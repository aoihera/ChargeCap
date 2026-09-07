/* SPDX-License-Identifier: MIT */

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "settings.hpp"

namespace settings {

    void Sanitize(ChargeCapConfig &cfg) {
        cfg.enabled = cfg.enabled ? 1 : 0;

        if (cfg.limit < CHARGECAP_LIMIT_MIN)
            cfg.limit = CHARGECAP_LIMIT_MIN;
        else if (cfg.limit > CHARGECAP_LIMIT_MAX)
            cfg.limit = CHARGECAP_LIMIT_MAX;

        cfg.reserved[0] = 0;
        cfg.reserved[1] = 0;
    }

    ChargeCapConfig Load() {
        /* Default to OFF (enabled=0) with default limit on first use / no config file */
        ChargeCapConfig cfg = { 0, CHARGECAP_LIMIT_DEFAULT, { 0, 0 } };

        std::FILE *f = std::fopen(CHARGECAP_CONFIG_PATH, "r");
        if (!f) {
            /* Upgrade path: the project used to be called charge-limit-NX
             * and stored its config elsewhere. Fall back to the old file so
             * a rename does not silently reset the user's settings. */
            f = std::fopen(CHARGECAP_LEGACY_CONFIG_PATH, "r");
        }
        if (f) {
            char line[96];
            while (std::fgets(line, sizeof(line), f)) {
                unsigned value = 0;
                if (std::sscanf(line, " enabled = %u", &value) == 1)
                    cfg.enabled = value ? 1 : 0;
                else if (std::sscanf(line, " limit = %u", &value) == 1)
                    cfg.limit = static_cast<u8>(value);
            }
            std::fclose(f);
        }

        Sanitize(cfg);
        return cfg;
    }

    void Save(const ChargeCapConfig &cfg) {
        mkdir("/config", 0777);
        mkdir(CHARGECAP_CONFIG_DIR, 0777);

        std::FILE *f = std::fopen(CHARGECAP_CONFIG_PATH, "w");
        if (!f)
            return;

        std::fprintf(f,
            "[ChargeCap]\n"
            "; 0 = off (no limit is applied at all), 1 = on\n"
            "enabled=%u\n"
            "; stop charging at this percentage (%u-%u)\n"
            "limit=%u\n",
            cfg.enabled ? 1u : 0u,
            (unsigned)CHARGECAP_LIMIT_MIN, (unsigned)CHARGECAP_LIMIT_MAX,
            (unsigned)cfg.limit);

        std::fclose(f);
    }

}
