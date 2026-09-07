/*
 * Minimal PSM battery charge control. See battery.h for provenance.
 *
 * Thin wrappers over libnx's psm bindings. The raw wire commands this module
 * needs (cmd 17 for the charge-info fields, cmds 2/3 to enable/disable
 * charging, cmd 0 for the percentage) are exactly what libnx's psm functions
 * issue, so calling those keeps the sysmodule off hand-rolled IPC while
 * gaining libnx's firmware-version handling for cmd 17: the info fields
 * struct grew at 17.0.0, and libnx sizes the response accordingly.
 */

#include <string.h>
#include "battery.h"

static bool g_init = false;

Result batteryInfoInitialize(void) {
    if (g_init)
        return 0;

    Result rc = psmInitialize();
    g_init = R_SUCCEEDED(rc);
    return rc;
}

void batteryInfoExit(void) {
    if (!g_init)
        return;
    psmExit();
    g_init = false;
}

/* BatteryChargeInfo mirrors the documented prefix of libnx's
 * PsmBatteryChargeInfoFields, which is layout-identical across firmware
 * versions (17.0.0 only appended trailing fields). */
_Static_assert(sizeof(PsmBatteryChargeInfoFields) >= sizeof(BatteryChargeInfo),
               "PsmBatteryChargeInfoFields must cover the BatteryChargeInfo prefix");

Result batteryInfoGetChargeInfo(BatteryChargeInfo *out) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    PsmBatteryChargeInfoFields fields = {0};

    /* libnx reads cmd 17 with the buffer size its firmware version expects
     * (0x40 before 17.0.0, 0x54 from 17.0.0 on). Copying only the prefix we
     * understand yields exactly the data the old raw 0x40-byte dispatch
     * produced on every firmware. */
    Result rc = psmGetBatteryChargeInfoFields(&fields);
    if (R_FAILED(rc))
        return rc;

    memcpy(out, &fields, sizeof(*out));
    return 0;
}

Result batteryInfoGetChargePercentage(u32 *out) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    return psmGetBatteryChargePercentage(out);
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
