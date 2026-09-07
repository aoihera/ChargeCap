/* SPDX-License-Identifier: MIT */
#pragma once

#include <chargecap.h>

namespace ipc {

    /* Fail-fast liveness probe: is the sysmodule process running?
     *
     * Uses pm:dmnt (pmdmntGetProcessId, initialized by the libultrahand
     * runtime) instead of asking sm for the service, so this can never block
     * or wedge the overlay host while the module is stopped. Same approach
     * ovl-sysmodules and KeyX use. */
    bool ModuleRunning();

    /* Tries to connect to the sysmodule. Safe to call repeatedly: it first
     * verifies the module is running (ModuleRunning) and only then asks sm
     * for the service, so a stopped module fails immediately and cheaply. */
    bool Connect();

    void Disconnect();

    /* Full teardown of everything this client holds: the ipc session (if
     * any) and the pm:dmnt reference taken by ModuleRunning(). Call once
     * from exitServices(). */
    void Exit();

    /* True if we currently hold a live session. */
    bool Available();

    bool GetConfig(ChargeCapConfig *out);
    bool SetConfig(const ChargeCapConfig &cfg);
    bool GetStatus(ChargeCapStatus *out);

}
