/* SPDX-License-Identifier: MIT */
#pragma once

#include <chargecap.h>

namespace settings {

    /* Reads the ini, falling back to sane defaults. */
    ChargeCapConfig Load();

    /* Writes the ini, creating /config/chargecap if needed.
     * The overlay owns persistence; the sysmodule only ever reads. */
    void Save(const ChargeCapConfig &cfg);

    void Sanitize(ChargeCapConfig &cfg);

}
