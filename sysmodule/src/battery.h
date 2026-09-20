/*
 * Battery charge gating and charge-state queries via psm.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <switch.h>

typedef struct {
    u8 charger_present;   /* non-zero if wall / dock power is supplied */
    u8 charging_enabled;  /* non-zero if the charge path is enabled */
} BatteryChargeInfo;

Result batteryInfoInitialize(void);
void   batteryInfoExit(void);
Result batteryInfoGetChargeInfo(BatteryChargeInfo *out);
bool   batteryInfoIsCharging(const BatteryChargeInfo *info);
Result batteryInfoGetChargePercentage(u32 *out);
Result batteryInfoGetRawChargePercentage(double *out);
Result batteryInfoEnableCharging(void);
Result batteryInfoDisableCharging(void);

/* Diagnostic only, same as batteryInfoGetRawChargePercentage: the charge
 * limit never consults this. Battery voltage average in mV, read via psm's
 * GetBatteryChargeInfoFields (cmd 17). */
Result batteryInfoGetVoltageMv(u32 *out_mv);
