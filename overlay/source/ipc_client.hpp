/* SPDX-License-Identifier: MIT */
#pragma once

#include <chargecap.h>

namespace ipc {

    bool ModuleRunning();
    void Exit();

    bool GetConfig(ChargeCapConfig *out);
    bool SetConfig(const ChargeCapConfig &cfg);
    bool GetStatus(ChargeCapStatus *out);

}
