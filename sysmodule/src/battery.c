/*
 * Battery charge gating and charge-state queries via psm.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <switch.h>
#include "battery.h"

static bool g_init = false;

Result batteryInfoInitialize(void) {
    if (g_init)
        return 0;

    Result rc = psmInitialize();
    if (R_SUCCEEDED(rc))
        g_init = true;

    return rc;
}

void batteryInfoExit(void) {
    if (!g_init)
        return;

    psmExit();
    g_init = false;
}

Result batteryInfoGetChargeInfo(BatteryChargeInfo *out) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    memset(out, 0, sizeof(*out));

    PsmChargerType type = PsmChargerType_Unconnected;
    Result rc = psmGetChargerType(&type);
    if (R_FAILED(rc))
        return rc;

    out->charger_present = (type != PsmChargerType_Unconnected) ? 1 : 0;

    bool enabled = false;
    rc = psmIsBatteryChargingEnabled(&enabled);
    if (R_FAILED(rc))
        return rc;

    out->charging_enabled = enabled ? 1 : 0;
    return 0;
}

bool batteryInfoIsCharging(const BatteryChargeInfo *info) {
    if (!info)
        return false;
    return (info->charger_present != 0) && (info->charging_enabled != 0);
}

Result batteryInfoGetChargePercentage(u32 *out) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    return psmGetBatteryChargePercentage(out);
}

/* The un-rounded gauge reading (psm cmd 12). Diagnostic only: the charge limit
 * runs off the rounded integer above; this exists so the overlay can surface
 * the fractional value the integer is rounded from and make gauge desync
 * visible. */
Result batteryInfoGetRawChargePercentage(double *out) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    return psmGetRawBatteryChargePercentage(out);
}

/* Battery voltage average in mV, via psm's GetBatteryChargeInfoFields (cmd 17). */
Result batteryInfoGetVoltageMv(u32 *out_mv) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out_mv)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    PsmBatteryChargeInfoFields fields;
    Result rc = psmGetBatteryChargeInfoFields(&fields);
    if (R_SUCCEEDED(rc))
        *out_mv = fields.battery_charge_milli_voltage;

    return rc;
}

Result batteryInfoEnableCharging(void) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return psmEnableBatteryCharging();
}

Result batteryInfoDisableCharging(void) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return psmDisableBatteryCharging();
}
