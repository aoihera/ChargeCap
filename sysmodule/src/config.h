/* SPDX-License-Identifier: MIT */
#pragma once

#include <chargecap.h>

/* Reads CHARGECAP_CONFIG_PATH. Always leaves *out in a valid state, even on
 * failure (disabled, default limit). Opens and closes fs itself so the
 * sysmodule holds no fs session while it is idle. */
void configLoad(ChargeCapConfig *out);

/* Clamps a config coming from IPC into something we are willing to enforce. */
void configSanitize(ChargeCapConfig *cfg);
