/*
 * Alarm Helper for ChargeCap
 *
 * Coordinates system wake alarms via time:al + time:s (CreateWakeupAlarm
 * with ISteadyClock nanosecond timepoint).
 *
 * Uses a persistent ISteadyClockAlarm session (Approach 2) with a
 * Disable -> Enable cycle to eliminate IPC session allocation churn.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <switch.h>
#include "alarm_helper.h"

/* Service handles */
static Service g_timeSSrv       = {0};
static Service g_steadyClockSrv = {0};
static Service g_timeAlSrv      = {0};
static Service g_alarmSubSrv    = {0};

/* Service states */
static bool g_steadyClockInit = false;
static bool g_timeAlInit      = false;

/* Scheduling tracking */
static bool g_scheduled   = false;
static u64  g_target_tick = 0;

/* Set before every Enable call, cleared only by a Disable that is CONFIRMED
 * successful (or by destroying the object). While it is false, no Enable has
 * been issued since the last confirmed disarm, so a cancellation Disable is a
 * guaranteed no-op - skipping it is what keeps the inert states (held at the
 * limit, unplugged, disabled) at zero alarm-service traffic. A failed Disable
 * keeps the flag set, so the next evaluation retries it. */
static bool g_armed_possible = false;

Result alarmHelperInit(void) {
    /* 1. Initialize time:s -> ISteadyClock for accurate steady-clock timepoints */
    if (!g_steadyClockInit) {
        Handle handle = INVALID_HANDLE;
        if (R_SUCCEEDED(smGetServiceOriginal(&handle, smEncodeName("time:s")))) {
            serviceCreate(&g_timeSSrv, handle);
            /* Cmd 2: GetStandardSteadyClock -> returns ISteadyClock */
            if (R_SUCCEEDED(serviceDispatch(&g_timeSSrv, 2, .out_num_objects = 1, .out_objects = &g_steadyClockSrv))) {
                g_steadyClockInit = true;
            }
        }
    }

    /* 2. Initialize time:al (IAlarmService) & create persistent ISteadyClockAlarm session */
    if (!g_timeAlInit) {
        Handle handle = INVALID_HANDLE;
        if (R_SUCCEEDED(smGetServiceOriginal(&handle, smEncodeName("time:al")))) {
            serviceCreate(&g_timeAlSrv, handle);
            g_timeAlInit = true;

            /* Cmd 0: CreateWakeupAlarm -> create persistent session once */
            (void)serviceDispatch(&g_timeAlSrv, 0, .out_num_objects = 1, .out_objects = &g_alarmSubSrv);
        }
    }

    return 0;
}

void alarmHelperExit(void) {
    alarmHelperCancel();

    if (g_steadyClockInit) {
        if (serviceIsActive(&g_steadyClockSrv))
            serviceClose(&g_steadyClockSrv);
        if (serviceIsActive(&g_timeSSrv))
            serviceClose(&g_timeSSrv);
        memset(&g_steadyClockSrv, 0, sizeof(Service));
        memset(&g_timeSSrv, 0, sizeof(Service));
        g_steadyClockInit = false;
    }

    if (g_timeAlInit) {
        if (serviceIsActive(&g_alarmSubSrv))
            serviceClose(&g_alarmSubSrv);
        if (serviceIsActive(&g_timeAlSrv))
            serviceClose(&g_timeAlSrv);
        memset(&g_alarmSubSrv, 0, sizeof(Service));
        memset(&g_timeAlSrv, 0, sizeof(Service));
        g_timeAlInit = false;
    }
}

Result alarmHelperSchedule(u32 delay_seconds) {
    if (delay_seconds == 0)
        return 0;

    const u64 now_tick   = armGetSystemTick();
    const u64 delay_tick = armNsToTicks((u64)delay_seconds * 1000000000ULL);
    bool scheduled       = false;

    if (g_timeAlInit && serviceIsActive(&g_timeAlSrv)) {
        /* Ensure persistent alarm session is open */
        if (!serviceIsActive(&g_alarmSubSrv)) {
            (void)serviceDispatch(&g_timeAlSrv, 0, .out_num_objects = 1, .out_objects = &g_alarmSubSrv);
        }

        if (serviceIsActive(&g_alarmSubSrv)) {
            u64 steady_time_point = 0;
            if (g_steadyClockInit && serviceIsActive(&g_steadyClockSrv)) {
                struct {
                    u64 time_point;
                    u8  source_id[16];
                } tp = {0};
                if (R_SUCCEEDED(serviceDispatchOut(&g_steadyClockSrv, 0, tp))) {
                    steady_time_point = tp.time_point;
                }
            }

            if (steady_time_point == 0)
                steady_time_point = armTicksToNs(now_tick);

            const u64 target_ns = steady_time_point + ((u64)delay_seconds * 1000000000ULL);

            /*
             * Approach 2: Persistent handle with Disable -> Enable cycle.
             * 1. Call Disable (Cmd 2) to acknowledge/reset the alarm state.
             * 2. Call Enable (Cmd 1) with the new target nanosecond timestamp.
             */
            serviceDispatch(&g_alarmSubSrv, 2); /* Cmd 2: Disable */

            g_armed_possible = true; /* before the call: a lost reply must not hide
                                      * a possibly-armed alarm from a later Disable */
            Result rc = serviceDispatchIn(&g_alarmSubSrv, 1, target_ns); /* Cmd 1: Enable */
            if (R_SUCCEEDED(rc)) {
                scheduled = true;
            } else {
                /* Fail-safe recovery: re-acquire session if Enable failed */
                serviceClose(&g_alarmSubSrv);
                g_armed_possible  = false; /* old object destroyed; the new one is
                                            * fresh (not enabled) */
                memset(&g_alarmSubSrv, 0, sizeof(Service));

                if (R_SUCCEEDED(serviceDispatch(&g_timeAlSrv, 0, .out_num_objects = 1, .out_objects = &g_alarmSubSrv))) {
                    g_armed_possible = true;
                    if (R_SUCCEEDED(serviceDispatchIn(&g_alarmSubSrv, 1, target_ns))) {
                        scheduled = true;
                    }
                }
            }
        }
    }

    if (scheduled) {
        g_target_tick = now_tick + delay_tick;
        g_scheduled   = true;
    } else {
        g_target_tick = 0;
        g_scheduled   = false;
    }

    return scheduled ? 0 : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
}

Result alarmHelperCancel(void) {
    /* Only talk to the alarm service if an Enable has been issued since the last
     * confirmed Disable: with nothing armed, cmd 2 was a guaranteed no-op sent on
     * every 5 s evaluation of the inert states (held at the limit, unplugged,
     * disabled). Every case where a Disable could matter still sends it - and a
     * failed Disable keeps the flag set, so it is retried on the next cancel. */
    if (g_armed_possible && serviceIsActive(&g_alarmSubSrv)) {
        if (R_SUCCEEDED(serviceDispatch(&g_alarmSubSrv, 2))) /* Cmd 2: Disable */
            g_armed_possible = false;
    }

    g_scheduled   = false;
    g_target_tick = 0;
    return 0;
}

bool alarmHelperIsScheduled(u32 *out_remaining_seconds) {
    if (!g_scheduled) {
        if (out_remaining_seconds)
            *out_remaining_seconds = 0;
        return false;
    }

    const u64 now_tick = armGetSystemTick();
    if (now_tick >= g_target_tick) {
        g_scheduled   = false;
        g_target_tick = 0;
        if (out_remaining_seconds)
            *out_remaining_seconds = 0;
        return false;
    }

    if (out_remaining_seconds) {
        const u64 remaining_ns = armTicksToNs(g_target_tick - now_tick);
        *out_remaining_seconds = (u32)(remaining_ns / 1000000000ULL);
    }

    return true;
}
