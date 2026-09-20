/* SPDX-License-Identifier: MIT */

#include <tesla.hpp>

#include "ipc_client.hpp"

namespace ipc {

    namespace {
        Service g_srv         = {};
        bool    g_hasSrv      = false;
        bool    g_pmdmntReady = false;

        void Disconnect() {
            if (!g_hasSrv)
                return;

            serviceClose(&g_srv);
            g_srv    = {};
            g_hasSrv = false;
        }

        bool Open() {
            Handle handle = INVALID_HANDLE;
            Result rc     = smGetServiceOriginal(&handle, smEncodeName(CHARGECAP_SERVICE_NAME));
            if (R_FAILED(rc))
                return false;

            serviceCreate(&g_srv, handle);
            g_hasSrv = true;
            return true;
        }

        bool Check(Result rc) {
            if (R_SUCCEEDED(rc))
                return true;

            Disconnect();
            return false;
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
    }

    bool ModuleRunning() {
        if (!g_pmdmntReady) {
            if (R_FAILED(pmdmntInitialize()))
                return false;
            g_pmdmntReady = true;
        }

        u64 pid = 0;
        return R_SUCCEEDED(pmdmntGetProcessId(&pid, CHARGECAP_PROGRAM_ID)) && pid != 0;
    }

    void Exit() {
        Disconnect();
        if (g_pmdmntReady) {
            pmdmntExit();
            g_pmdmntReady = false;
        }
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
