/*
 * Minimal MAX17050 fuel-gauge access over I2C.
 *
 * Diagnostic only: the charge limit never consults this. It exists so the
 * overlay can show the battery's actual cell voltage next to the gauge's
 * reported percentage - a percentage that disagrees with the voltage (e.g. a
 * "full" voltage at a low reported percent) is the unambiguous tell of gauge
 * desync, which a percentage-only readout cannot reveal.
 *
 * The I2C access pattern (one session per read, SendAuto the register then
 * ReceiveAuto the value) mirrors hanai3Bi/Switch-OC-Suite's i2c.c (GPLv2).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <switch.h>

Result fuelGaugeInitialize(void);
void   fuelGaugeExit(void);

/* Reads the MAX17050 VCELL register and converts it to millivolts. */
Result fuelGaugeGetCellVoltageMv(u32 *out_mv);
