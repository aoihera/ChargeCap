/*
 * ChargeCap - shared definitions between the sysmodule and the overlay.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

/* Program ID of the sysmodule. Unchanged from the project's earlier name
 * (charge-limit-NX); a title id is an address, not a name, and changing it
 * would strand old installs. "CLNX" in the low 32 bits. */
#define CHARGECAP_PROGRAM_ID     0x42000000000000C0ULL
#define CHARGECAP_PROGRAM_ID_STR "42000000000000C0"

/* SM service name. Must be <= 8 bytes (smEncodeName + ovl-sysmodules limit). */
#define CHARGECAP_SERVICE_NAME  "chgcap"

/* Accepted charge limit range, inclusive. */
#define CHARGECAP_LIMIT_MIN      50
#define CHARGECAP_LIMIT_MAX      99
#define CHARGECAP_LIMIT_DEFAULT  80
#define CHARGECAP_LIMIT_STEPS    (CHARGECAP_LIMIT_MAX - CHARGECAP_LIMIT_MIN + 1)

/* Config file. Owned (written) by the overlay, read at boot by the sysmodule. */
#define CHARGECAP_CONFIG_DIR    "/config/chargecap"
#define CHARGECAP_CONFIG_PATH   "/config/chargecap/config.ini"

/* One-time upgrade path: the project used to be called charge-limit-NX and
 * stored its config at /config/charge-limit-NX/config.ini. Both the sysmodule
 * and the overlay fall back to reading that file when the new one does not
 * exist yet, so existing settings survive the rename. Nothing is ever
 * written there; the overlay rewrites the new path on the next save.
 * Delete these once you no longer need to support old installs. */
#define CHARGECAP_LEGACY_CONFIG_PATH "/config/charge-limit-NX/config.ini"

/* How often the sysmodule re-evaluates the battery state. It also re-evaluates
 * on demand whenever a client asks for the status, so an open overlay refreshes
 * (and enforces) at the overlay's own cadence. */
#define CHARGECAP_POLL_NS       5000000000ULL

typedef enum {
    /* void -> ChargeCapConfig */
    ChargeCapCmd_GetConfig = 0,
    /* ChargeCapConfig -> void (applied immediately, not persisted) */
    ChargeCapCmd_SetConfig = 1,
    /* void -> ChargeCapStatus */
    ChargeCapCmd_GetStatus = 2,
} ChargeCapCmd;

typedef struct {
    u8 enabled;   /* 0 = do nothing at all, 1 = enforce the limit */
    u8 limit;     /* percentage, CHARGECAP_LIMIT_MIN .. CHARGECAP_LIMIT_MAX */
    u8 reserved[2];
} ChargeCapConfig;

typedef struct {
    u8 charge_percent;
    u8 charger_connected;
    u8 charging;   /* psm charge-enable bit, as of this evaluation */
    u8 limit_held; /* 1 when we are actively holding charging off */
} ChargeCapStatus;

#ifdef __cplusplus
}
#endif
