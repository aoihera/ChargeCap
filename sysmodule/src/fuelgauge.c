/* SPDX-License-Identifier: MIT
 *
 * See fuelgauge.h for what this is and why it is diagnostic-only.
 */

#include "fuelgauge.h"

/* MAX17050 register map. Only the instantaneous cell voltage is needed. */
#define MAX17050_VCELL_REG 0x09

static bool g_init = false;

Result fuelGaugeInitialize(void) {
    if (g_init)
        return 0;

    Result rc = i2cInitialize();
    g_init = R_SUCCEEDED(rc);
    return rc;
}

void fuelGaugeExit(void) {
    if (!g_init)
        return;
    i2cExit();
    g_init = false;
}

/* One session per read. The device answers a register write (the address)
 * followed by a 16-bit little-endian read, so both halves must complete or the
 * value is meaningless - close and surface the error either way. */
static Result readReg16(u8 reg, u16 *out) {
    I2cSession s;
    Result rc = i2cOpenSession(&s, I2cDevice_Max17050);
    if (R_FAILED(rc))
        return rc;

    const struct { u8 reg; } __attribute__((packed)) cmd = { reg };
    rc = i2csessionSendAuto(&s, &cmd, sizeof(cmd), I2cTransactionOption_All);
    if (R_SUCCEEDED(rc)) {
        u16 val = 0;
        rc = i2csessionReceiveAuto(&s, &val, sizeof(val), I2cTransactionOption_All);
        if (R_SUCCEEDED(rc))
            *out = val;
    }

    i2csessionClose(&s);
    return rc;
}

Result fuelGaugeGetCellVoltageMv(u32 *out_mv) {
    if (!g_init)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!out_mv)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u16 raw = 0;
    Result rc = readReg16(MAX17050_VCELL_REG, &raw);
    if (R_FAILED(rc))
        return rc;

    /* MAX17050 VCELL resolution is 0.078125 mV per LSB (== 625/8000 mV). Kept
     * as integer math so this pulls in no soft-float; raw * 625 tops out at
     * ~41M, well inside u32, so there is no need to pre-shift and lose the low
     * bits of precision. */
    *out_mv = ((u32)raw * 625u) / 8000u;
    return 0;
}
