/*
 * Shared types and constants for ChargeCap (sysmodule + overlay).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <switch.h>

/* Program ID of the sysmodule */
#define CHARGECAP_PROGRAM_ID     0x42000000000000C0ULL
#define CHARGECAP_PROGRAM_ID_STR "42000000000000C0"

/* SM service name */
#define CHARGECAP_SERVICE_NAME "chgcap"

/* Accepted charge limit range, inclusive */
#define CHARGECAP_LIMIT_MIN     50
#define CHARGECAP_LIMIT_MAX     99
#define CHARGECAP_LIMIT_DEFAULT 80
#define CHARGECAP_LIMIT_STEPS   (CHARGECAP_LIMIT_MAX - CHARGECAP_LIMIT_MIN + 1)

/* Config paths */
#define CHARGECAP_CONFIG_DIR    "/config/chargecap"
#define CHARGECAP_CONFIG_PATH   "/config/chargecap/config.ini"
#define CHARGECAP_LEGACY_CONFIG_PATH "/config/charge-limit-NX/config.ini"

/* How often the sysmodule re-evaluates the battery state while awake */
#define CHARGECAP_POLL_NS 5000000000ULL

typedef enum {
    ChargeCapCmd_GetConfig = 0,
    ChargeCapCmd_SetConfig = 1,
    ChargeCapCmd_GetStatus = 2,
} ChargeCapCmd;

typedef struct {
    u8 enabled;
    u8 limit;
    u8 sleep_limit_enabled; /* 1 to periodically wake Switch in screen-off background to enforce limit while asleep */
    u8 reserved;
} ChargeCapConfig;

typedef struct {
    u8  charge_percent;        /* psmGetBatteryChargePercentage, rounded 0..100 */
    u8  charger_connected;
    u8  charging;              /* psm charge-enable bit, as of this evaluation */
    u8  limit_held;            /* 1 when we are actively holding charging off */
    u8  alarm_active;          /* 1 if an RTC sleep-wake alarm is currently armed */
    u8  reserved;
    u16 next_alarm_seconds;    /* Seconds remaining until the next sleep-wake check */
    u16 raw_permille;          /* raw, un-rounded fuel-gauge reading in tenths of a percent (798 == 79.8%). 0xFFFF == unavailable */
    u16 cell_mv;               /* battery voltage average in mV, from psm's charge-info fields. 0xFFFF == unavailable */
} ChargeCapStatus;

#ifdef __cplusplus
static_assert(sizeof(ChargeCapConfig) == 4, "ChargeCapConfig size mismatch");
static_assert(sizeof(ChargeCapStatus) == 12, "ChargeCapStatus size mismatch");
#endif
