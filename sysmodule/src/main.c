/*
 * ChargeCap sysmodule
 *
 * Single purpose: stop charging once the battery reaches a user-set
 * percentage, and resume when it falls back below it.
 *
 * Includes optional Smart RTC Alarm Sleep-Wake Loop:
 * When enabled, automatically schedules hardware RTC wake alarms while asleep
 * so charging reaches the exact target limit even in sleep mode.
 * Sets alarm for X = (limit - percent) minutes when >2% away, and when within 2%
 * sets 10-second recurring wake alarms until the exact target limit is reached.
 *
 * Includes diagnostic readouts: un-rounded raw gauge permille and battery
 * voltage (both read via psm).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <switch.h>

#include <chargecap.h>
#include "battery.h"
#include "config.h"
#include "ipc_server.h"
#include "alarm_helper.h"

#define INNER_HEAP_SIZE 0x800
#define MAX_SESSIONS 2
#define ERROR_BACKOFF_NS 100000000ULL
#define CHARGECAP_DIAG_POLL_NS 5000000000ULL

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

    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
}

void __appExit(void) {
    alarmHelperExit();
    smExit();
}

static IpcServer       g_server;
static ChargeCapConfig g_cfg;
static ChargeCapStatus g_status;

static bool g_hold_wanted    = false;
static bool g_we_disabled    = false;
static bool g_reconciled     = false;
static bool g_check_owed     = true;   /* did the last completed evaluation leave
                                        * something owed? (charging on below the
                                        * limit, or a stop that did not take) */

static u64  g_last_diag_tick = 0;

/* Diagnostic-only readouts. The charge limit itself never consults either of
 * these; they exist purely for the overlay to display. */
static void UpdateDiagnostics(void) {
    const u64 now = armGetSystemTick();
    if (g_last_diag_tick != 0 && now < g_last_diag_tick + armNsToTicks(CHARGECAP_DIAG_POLL_NS))
        return;

    g_last_diag_tick = now;

    /* Raw unrounded gauge percentage (psm cmd 12) */
    double raw = 0.0;
    if (R_SUCCEEDED(batteryInfoGetRawChargePercentage(&raw))) {
        if (raw < 0.0)
            raw = 0.0;
        else if (raw > 100.0)
            raw = 100.0;
        g_status.raw_permille = (u16)(raw * 10.0 + 0.5);
    } else {
        g_status.raw_permille = 0xFFFF;
    }

    /* Battery voltage average in mV, via psm (see batteryInfoGetVoltageMv) */
    u32 cell_mv = 0;
    if (R_SUCCEEDED(batteryInfoGetVoltageMv(&cell_mv))) {
        if (cell_mv > 0xFFFE)
            cell_mv = 0xFFFE;
        g_status.cell_mv = (u16)cell_mv;
    } else {
        g_status.cell_mv = 0xFFFF;
    }
}

static void ApplyChargeLimit(void) {
    BatteryChargeInfo info;
    u32               percent = 0;
    Result            rc_info = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    Result            rc_pct  = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    /* Wake fence: arm a check BEFORE reading the gauge, but only when the
     * last completed evaluation left something owed (charging on below the
     * limit, or a stop that did not take). If this evaluation is then cut
     * short below, the console still wakes in 10 s and retries instead of
     * sleeping with nothing armed. In the settled states - held at the limit
     * with charging off, unplugged, disabled - nothing is owed, the fence is
     * skipped and an idle console does zero alarm traffic. The decision
     * further down replaces the fence with the correct delay - or cancels
     * it - in the same pass. */
    if (g_cfg.enabled && g_cfg.sleep_limit_enabled && g_check_owed)
        alarmHelperSchedule(10);

    /* Fast retry loop for wake transitions (PMIC fuel gauge stabilization) */
    for (int retry = 0; retry < 5; retry++) {
        rc_info = batteryInfoGetChargeInfo(&info);
        rc_pct  = batteryInfoGetChargePercentage(&percent);
        if (R_SUCCEEDED(rc_info) && R_SUCCEEDED(rc_pct))
            break;
        svcSleepThread(5000000ULL); /* 5ms */
    }

    if (R_FAILED(rc_info) || R_FAILED(rc_pct))
        return;

    if (percent > 100)
        percent = 100;

    bool       charging = batteryInfoIsCharging(&info);
    const bool plugged  = (info.charger_present != 0);

    g_status.charge_percent    = (u8)percent;
    g_status.charger_connected = plugged ? 1 : 0;

    UpdateDiagnostics();

    if (!plugged) {
        /* Unplugged -> cancel any pending RTC sleep-wake alarm */
        alarmHelperCancel();
        g_status.alarm_active       = 0;
        g_status.next_alarm_seconds = 0;

        g_hold_wanted = false;
        if (g_we_disabled) {
            if (R_SUCCEEDED(batteryInfoEnableCharging())) {
                g_we_disabled = false;
                charging      = true;
            }
        }

        g_status.charging   = charging ? 1 : 0;
        g_status.limit_held = 0;
        g_check_owed        = false;
        return;
    }

    /* ------------------------------------------------------------------
     * Decide what charging and alarms should be doing.
     * ------------------------------------------------------------------ */
    if (!g_cfg.enabled) {
        /* Off means off: cancel alarms and re-arm charging */
        alarmHelperCancel();
        g_status.alarm_active       = 0;
        g_status.next_alarm_seconds = 0;
        g_hold_wanted               = false;
    } else if (percent >= g_cfg.limit) {
        /* Target limit reached! Cut charging immediately and cancel pending alarms */
        alarmHelperCancel();
        g_status.alarm_active       = 0;
        g_status.next_alarm_seconds = 0;
        g_hold_wanted               = true;
    } else {
        /* Below limit */
        g_hold_wanted = false;

        /* If optional sleep-wake limit is enabled, schedule adaptive RTC wake alarm */
        if (g_cfg.sleep_limit_enabled) {
            /* Enter rapid 10s polling when within 2% of limit to guarantee 0% overshoot.
             * For coarse approach, aim to wake at (limit - 2) using a safe 40s/1% rate. */
            const u32 coarse_target = (g_cfg.limit > 2) ? (g_cfg.limit - 2) : 0;

            if (percent >= coarse_target) {
                /* Within 2% of limit (e.g. 58% or 59% for limit 60%): 10s recurring checks */
                alarmHelperSchedule(10);
            } else {
                /* Far approach: wake up when reaching (limit - 2), budgeting 40 seconds per 1% */
                const u32 diff_to_coarse = coarse_target - percent;
                const u32 delay_seconds  = diff_to_coarse * 40;
                alarmHelperSchedule(delay_seconds);
            }

            u32 remaining = 0;
            if (alarmHelperIsScheduled(&remaining)) {
                g_status.alarm_active       = 1;
                g_status.next_alarm_seconds = (u16)remaining;
            } else {
                g_status.alarm_active       = 0;
                g_status.next_alarm_seconds = 0;
            }
        } else {
            alarmHelperCancel();
            g_status.alarm_active       = 0;
            g_status.next_alarm_seconds = 0;
        }
    }

    /* ------------------------------------------------------------------
     * Reconcile intent against psm hardware state.
     * ------------------------------------------------------------------ */
    if (g_hold_wanted) {
        if (charging || !g_we_disabled) {
            for (int r = 0; r < 3; r++) {
                if (R_SUCCEEDED(batteryInfoDisableCharging())) {
                    g_we_disabled = true;
                    charging      = false;
                    break;
                }
                svcSleepThread(5000000ULL); /* 5ms */
            }
        }
    } else if (!charging && (g_we_disabled || !g_reconciled)) {
        if (R_SUCCEEDED(batteryInfoEnableCharging())) {
            g_we_disabled = false;
            charging      = true;
        }
    }

    g_reconciled = true;

    /* If we wanted a hold but charging is still on (the stop failed), do not
     * sleep unarmed: keep a check pending so the stop is retried. Report what
     * actually happened - if even this arm failed, the status must not claim a
     * retry that does not exist. Same readback pattern as the decision above. */
    if (g_hold_wanted && charging && g_cfg.sleep_limit_enabled) {
        alarmHelperSchedule(10);

        u32 remaining = 0;
        if (alarmHelperIsScheduled(&remaining)) {
            g_status.alarm_active       = 1;
            g_status.next_alarm_seconds = (u16)remaining;
        } else {
            g_status.alarm_active       = 0;
            g_status.next_alarm_seconds = 0;
        }
    }

    /* What the next evaluation owes: only states with charging still on need
     * the wake chain kept alive. Held-at-limit with charging off, unplugged
     * and disabled states owe nothing. */
    g_check_owed = (g_cfg.enabled && g_cfg.sleep_limit_enabled && plugged &&
                    (!g_hold_wanted || charging));

    g_status.charging   = charging ? 1 : 0;
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
            g_hold_wanted = false;
            g_check_owed  = true;   /* config changed: evaluate fenced */
            ApplyChargeLimit();
            return 0;

        case ChargeCapCmd_GetStatus:
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

    alarmHelperInit();

    if (R_FAILED(ipcServerInit(&g_server, CHARGECAP_SERVICE_NAME, MAX_SESSIONS))) {
        batteryInfoExit();
        alarmHelperExit();
        return 1;
    }

    ApplyChargeLimit();

    u64 last_tick = armGetSystemTick();
    u64 poll_ns   = CHARGECAP_POLL_NS;
    u64 next_tick = last_tick + armNsToTicks(poll_ns);

    while (true) {
        const u64 now     = armGetSystemTick();
        u64       timeout = 0;

        if (next_tick > now)
            timeout = armTicksToNs(next_tick - now);

        Result rc = ipcServerProcess(&g_server, RequestHandler, NULL, timeout);

        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut))
            svcSleepThread(ERROR_BACKOFF_NS);

        const u64 cur_tick = armGetSystemTick();

        /* Detect wake transition from sleep (> 7s gap) or normal poll timeout */
        if (cur_tick > last_tick + armNsToTicks(CHARGECAP_POLL_NS + 2000000000ULL)) {
            ApplyChargeLimit();
            last_tick = armGetSystemTick();
        } else if (cur_tick >= next_tick) {
            ApplyChargeLimit();
            last_tick = armGetSystemTick();
        }

        next_tick = last_tick + armNsToTicks(poll_ns);
    }
}
