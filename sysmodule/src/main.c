/*
 * ChargeCap sysmodule
 *
 * Single purpose: stop charging once the battery reaches a user-set
 * percentage, and resume when it falls back below it.
 *
 * Design notes, all in service of a small footprint:
 *  - Pure C. No C++ runtime, no static constructors, no exception tables.
 *  - One thread. The IPC wait doubles as the poll timer, so there is no
 *    second thread and no second thread stack.
 *  - No fsdev / stdio. The config file is read once at boot through raw fs,
 *    then the fs session is released.
 *  - Tiny inner heap. Nothing here allocates after init.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <switch.h>

#include <chargecap.h>
#include "battery.h"
#include "config.h"
#include "ipc_server.h"

/* Nothing in this module allocates after init; this only has to cover libnx's
 * own bookkeeping. Raise it if you add anything that mallocs. */
#define INNER_HEAP_SIZE 0x4000

/* The overlay is the only client. One spare session for a second reader. */
#define MAX_SESSIONS 2

/* Backoff after an unexpected ipcServerProcess() failure, so a permanently
 * broken handle degrades into a slow retry instead of a 100% CPU spin. */
#define ERROR_BACKOFF_NS 100000000ULL

void virtmemSetup(void);

u32    __nx_applet_type      = AppletType_None;
u32    __nx_fs_num_sessions  = 1;
size_t nx_inner_heap_size    = INNER_HEAP_SIZE;
char   nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void) {
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = nx_inner_heap;
    fake_heap_end   = nx_inner_heap + nx_inner_heap_size;

    virtmemSetup();
}

void __appInit(void) {
    if (R_FAILED(smInitialize()))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    /* Some libnx paths consult the firmware version. Set it once, then drop
     * the set:sys session again. */
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
}

void __appExit(void) {
    smExit();
}

static IpcServer    g_server;
static ChargeCapConfig g_cfg;
static ChargeCapStatus g_status;

/* --------------------------------------------------------------------------
 * Charge state machine
 *
 * Three pieces of state, and the distinction between them is the whole point:
 *
 *  g_hold_wanted  what the limit says charging *should* be doing.
 *  g_we_disabled  whether the last disable request came from us. We only ever
 *                 undo our own stop: Horizon disables charging on its own for
 *                 a full pack, a weak charger or a hot battery, and blindly
 *                 re-enabling every tick would fight those.
 *  g_reconciled   whether we have evaluated at least once with a charger
 *                 attached. Guards a single boot-time recovery, because a
 *                 previous instance of this module may have been killed
 *                 (ovl-sysmodules toggle, crash, update) while it was holding
 *                 charging off, and that hold lives in the PMIC, not here.
 * -------------------------------------------------------------------------- */
static bool g_hold_wanted = false;
static bool g_we_disabled = false;
static bool g_reconciled  = false;

static void ApplyChargeLimit(void) {
    BatteryChargeInfo info;
    u32               percent = 0;

    /* Never make a decision from data we failed to read. The old code fell
     * through with percent == 0, which reads as "battery empty" and resumes
     * charging on a single failed psm call. */
    if (R_FAILED(batteryInfoGetChargeInfo(&info)) ||
        R_FAILED(batteryInfoGetChargePercentage(&percent))) {
        return;
    }

    if (percent > 100)
        percent = 100;

    bool       charging = batteryInfoIsCharging(&info);
    const bool plugged  = (info.charger_present != 0);

    g_status.charge_percent    = (u8)percent;
    g_status.charger_connected = plugged ? 1 : 0;

    if (!plugged) {
        /* Nothing to gate. Drop the latch so the next plug-in starts clean,
         * and make sure we never leave the charge path armed off. */
        g_hold_wanted = false;
        if (g_we_disabled) {
            if (R_SUCCEEDED(batteryInfoEnableCharging())) {
                g_we_disabled = false;
                charging      = true;
            }
        }

        g_status.charging   = charging ? 1 : 0;
        g_status.limit_held = 0;
        return;
    }

    /* ------------------------------------------------------------------
     * Decide what charging should be doing.
     * ------------------------------------------------------------------ */
    if (!g_cfg.enabled) {
        /* Off means off. We still fall through to the reconcile step below so
         * that a hold left behind by a previous run gets released. */
        g_hold_wanted = false;
    } else if (percent >= g_cfg.limit) {
        g_hold_wanted = true;
    } else {
        g_hold_wanted = false;
    }

    /* ------------------------------------------------------------------
     * Reconcile intent against what psm actually reports.
     *
     * The disable side is driven purely by the hardware bit, never by our own
     * bookkeeping. Horizon re-arms charging behind our back across wake from
     * deep sleep, psm restarts and charger re-detection; the old code short
     * circuited on `!g_held` and so silently stopped enforcing the limit for
     * the rest of the charge cycle whenever that happened.
     * ------------------------------------------------------------------ */
    if (g_hold_wanted) {
        if (charging && R_SUCCEEDED(batteryInfoDisableCharging())) {
            g_we_disabled = true;
            charging      = false;
        }
    } else if (!charging && (g_we_disabled || !g_reconciled)) {
        if (R_SUCCEEDED(batteryInfoEnableCharging())) {
            g_we_disabled = false;
            charging      = true;
        }
    }

    g_reconciled = true;

    g_status.charging   = charging ? 1 : 0;
    /* Report what charging is actually doing, not what we asked for: if the
     * disable call failed this tick, the limit is not being held yet. */
    g_status.limit_held = (g_hold_wanted && !charging) ? 1 : 0;
}

static Result RequestHandler(void *userdata, const IpcServerRequest *r, u8 *out_data, size_t *out_dataSize) {
    (void)userdata;

    switch (r->data.cmdId) {
        case ChargeCapCmd_GetConfig:
            *out_dataSize = sizeof(ChargeCapConfig);
            memcpy(out_data, &g_cfg, sizeof(ChargeCapConfig));
            return 0;

        case ChargeCapCmd_SetConfig:
            if (r->data.size < sizeof(ChargeCapConfig))
                break;
            memcpy(&g_cfg, r->data.ptr, sizeof(ChargeCapConfig));
            configSanitize(&g_cfg);
            /* Re-evaluate immediately after every config change. */
            g_hold_wanted = false;
            ApplyChargeLimit();
            return 0;

        case ChargeCapCmd_GetStatus:
            /* Re-evaluate on read so an open overlay shows live values (and
             * enforces at its own refresh rate) rather than up to one poll
             * interval of stale data. */
            ApplyChargeLimit();
            *out_dataSize = sizeof(ChargeCapStatus);
            memcpy(out_data, &g_status, sizeof(ChargeCapStatus));
            return 0;

        default:
            break;
    }

    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    memset(&g_status, 0, sizeof(g_status));
    configLoad(&g_cfg);

    if (R_FAILED(batteryInfoInitialize()))
        return 1;

    if (R_FAILED(ipcServerInit(&g_server, CHARGECAP_SERVICE_NAME, MAX_SESSIONS))) {
        batteryInfoExit();
        return 1;
    }

    /* First evaluation doubles as recovery for a hold left in the PMIC by a
     * previous instance. There is no exit path to clean up in: this process is
     * torn down with svcTerminateProcess (ovl-sysmodules, shutdown), so no
     * teardown code of ours ever runs. Recovery has to happen on the way in. */
    ApplyChargeLimit();

    u64 next_tick = armGetSystemTick() + armNsToTicks(CHARGECAP_POLL_NS);

    while (true) {
        const u64 now     = armGetSystemTick();
        u64       timeout = 0;

        if (next_tick > now)
            timeout = armTicksToNs(next_tick - now);

        Result rc = ipcServerProcess(&g_server, RequestHandler, NULL, timeout);

        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut))
            svcSleepThread(ERROR_BACKOFF_NS);

        /* Tick on a deadline, not on "the wait timed out". Any IPC traffic
         * used to reset the timeout from scratch, so with the overlay open and
         * polling every second the limit was never re-evaluated at all. */
        if (armGetSystemTick() >= next_tick) {
            ApplyChargeLimit();
            next_tick = armGetSystemTick() + armNsToTicks(CHARGECAP_POLL_NS);
        }
    }

    /* Not reached. See the comment above the first ApplyChargeLimit(). */
}
