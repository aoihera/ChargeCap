/*
 * Minimal PSM battery charge control.
 *
 * Trimmed down from Horizon-OC's hoc-clk (Source/hoc-clk/sysmodule/src/pwr),
 * which in turn carries the charge-limit plumbing originally implemented in
 * hanai3Bi/Switch-OC-Suite. Only what the charge limit needs is kept: the
 * charge info struct, the charging flag, and enable/disable charging.
 *
 * Original: Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 *           (GPLv2). See THIRD-PARTY-NOTICES.md.
 */

#pragma once

#include <switch.h>

/* Layout of PSM cmd 17 (GetBatteryChargeInfoFields). */
typedef struct {
    s32 InputCurrentLimit;
    s32 VBUSCurrentLimit;
    s32 ChargeCurrentLimit;
    s32 ChargeVoltageLimit;
    s32 unk_x10;
    s32 unk_x14;            /* bit 8 = charging enabled */
    s32 PDControllerState;
    s32 reserved_x1c;
    s32 reserved_x20;
    s32 reserved_x24;
    s32 reserved_x28;
    s32 PowerRole;
    s32 charger_present;    /* raw PSM charger code, only zero/nonzero matters */
    s32 reserved_x34;
    s32 reserved_x38;
    s32 reserved_x3c;
} BatteryChargeInfo;

#define IS_BATTERY_CHARGING_ENABLED(info) (((info)->unk_x14 >> 8) & 1)

static inline bool batteryInfoIsCharging(const BatteryChargeInfo *info) {
    return IS_BATTERY_CHARGING_ENABLED(info) != 0;
}

Result batteryInfoInitialize(void);
void   batteryInfoExit(void);
Result batteryInfoGetChargeInfo(BatteryChargeInfo *out);
Result batteryInfoGetChargePercentage(u32 *out);
Result batteryInfoEnableCharging(void);
Result batteryInfoDisableCharging(void);
