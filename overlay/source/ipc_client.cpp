/* SPDX-License-Identifier: MIT */

#include <tesla.hpp>

#include "ipc_client.hpp"

namespace ipc {

    namespace {
        Service g_srv    = {};
        bool    g_hasSrv = false;

        /* True once we have taken our own pm:dmnt reference (see
         * ModuleRunning). Exit() balances exactly this reference. */
        bool    g_pmdmntReady = false;

        /* A missing sysmodule is an expected state, not a transient IPC
         * failure. Ask sm for the service only after ModuleRunning() has
         * confirmed the module process exists.
         *
         * smGetService() blocks forever when the name is not registered, which
         * would hang the overlay whenever the sysmodule is stopped.
         * smGetServiceOriginal() fails immediately instead. Same trick
         * ovl-sysmodules uses. */
        bool Open() {
            Handle handle = INVALID_HANDLE;
            Result rc     = smGetServiceOriginal(&handle, smEncodeName(CHARGECAP_SERVICE_NAME));
            if (R_FAILED(rc))
                return false;

            serviceCreate(&g_srv, handle);
            g_hasSrv = true;
            return true;
        }

        /* A dead session means the module was stopped; drop it so the next
         * Connect() can pick it up again if it comes back. */
        bool Check(Result rc) {
            if (R_SUCCEEDED(rc))
                return true;

            Disconnect();
            return false;
        }
    }

    bool ModuleRunning() {
        /* pm:dmnt is initialized by the libultrahand runtime (__appInit).
         * Lazy-init anyway so this also works under older hosts; the session
         * is ref-counted by libnx, and Exit() later drops only the reference
         * we took here. */
        if (!g_pmdmntReady) {
            if (R_FAILED(pmdmntInitialize()))
                return false;
            g_pmdmntReady = true;
        }

        u64 pid = 0;
        return R_SUCCEEDED(pmdmntGetProcessId(&pid, CHARGECAP_PROGRAM_ID)) && pid != 0;
    }

    bool Connect() {
        if (g_hasSrv)
            return true;
        if (!ModuleRunning())
            return false;

        bool ok = false;
        tsl::hlp::doWithSmSession([&ok] {
            ok = Open();
        });

        return ok;
    }

    void Disconnect() {
        if (!g_hasSrv)
            return;

        serviceClose(&g_srv);
        g_srv    = {};
        g_hasSrv = false;
    }

    void Exit() {
        Disconnect();

        /* Balance the pm:dmnt reference ModuleRunning() took, if any. The
         * libultrahand runtime may hold its own reference (it initializes
         * pm:dmnt in __appInit), which we must not touch - libnx ref-counts,
         * so dropping only ours is safe either way. */
        if (g_pmdmntReady) {
            pmdmntExit();
            g_pmdmntReady = false;
        }
    }

    bool Available() {
        return g_hasSrv;
    }

    bool GetConfig(ChargeCapConfig *out) {
        if (!Connect())
            return false;

        return Check(serviceDispatchOut(&g_srv, ChargeCapCmd_GetConfig, *out));
    }

    bool SetConfig(const ChargeCapConfig &cfg) {
        if (!Connect())
            return false;

        return Check(serviceDispatchIn(&g_srv, ChargeCapCmd_SetConfig, cfg));
    }

    bool GetStatus(ChargeCapStatus *out) {
        if (!Connect())
            return false;

        return Check(serviceDispatchOut(&g_srv, ChargeCapCmd_GetStatus, *out));
    }

}
