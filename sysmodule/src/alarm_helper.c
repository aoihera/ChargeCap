/*
 * Alarm Helper for ChargeCap
 *
 * Coordinates hardware system wake alarms via:
 * 1. time:al + time:s (CreateWakeupAlarm with ISteadyClock nanosecond timepoint)
 * 2. rtc (IRtcManager hardware PMIC RTC alarms fallback)
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <switch.h>
#include "alarm_helper.h"

#define RTC_DEVICE_CODE 0x3B000001
#define RTC_ALARM_ID    0

/* Service handles */
static Service g_timeSSrv       = {0};
static Service g_steadyClockSrv = {0};
static Service g_timeAlSrv      = {0};
static Service g_alarmSubSrv    = {0};
static Service g_rtcSrv         = {0};

/* Service states */
static bool g_steadyClockInit = false;
static bool g_timeAlInit      = false;
static bool g_rtcInit         = false;

/* Scheduling tracking */
static bool g_scheduled   = false;
static u64  g_target_tick = 0;

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

    /* 2. Initialize time:al (IAlarmService) */
    if (!g_timeAlInit) {
        Handle handle = INVALID_HANDLE;
        if (R_SUCCEEDED(smGetServiceOriginal(&handle, smEncodeName("time:al")))) {
            serviceCreate(&g_timeAlSrv, handle);
            g_timeAlInit = true;
        }
    }

    /* 3. Initialize rtc (IRtcManager fallback) */
    if (!g_rtcInit) {
        Handle handle = INVALID_HANDLE;
        if (R_SUCCEEDED(smGetServiceOriginal(&handle, smEncodeName("rtc")))) {
            serviceCreate(&g_rtcSrv, handle);
            g_rtcInit = true;
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

    if (g_rtcInit) {
        if (serviceIsActive(&g_rtcSrv))
            serviceClose(&g_rtcSrv);
        memset(&g_rtcSrv, 0, sizeof(Service));
        g_rtcInit = false;
    }
}

Result alarmHelperSchedule(u32 delay_seconds) {
    if (delay_seconds == 0)
        return 0;

    const u64 now_tick   = armGetSystemTick();
    const u64 delay_tick = armNsToTicks((u64)delay_seconds * 1000000000ULL);
    g_target_tick        = now_tick + delay_tick;
    g_scheduled          = true;

    /* 1. Schedule via time:al: Obtain fresh ISteadyClockAlarm via CreateWakeupAlarm (Cmd 0) */
    if (g_timeAlInit && serviceIsActive(&g_timeAlSrv)) {
        if (serviceIsActive(&g_alarmSubSrv)) {
            serviceDispatch(&g_alarmSubSrv, 2); /* Disable previous */
            serviceClose(&g_alarmSubSrv);
            memset(&g_alarmSubSrv, 0, sizeof(Service));
        }

        /* Cmd 0: CreateWakeupAlarm -> returns fresh ISteadyClockAlarm */
        if (R_SUCCEEDED(serviceDispatch(&g_timeAlSrv, 0, .out_num_objects = 1, .out_objects = &g_alarmSubSrv))) {
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
            serviceDispatchIn(&g_alarmSubSrv, 1, target_ns); /* Enable */
        }
    }

    /* 2. Schedule via rtc (IRtcManager hardware PMIC RTC alarm fallback) */
    if (g_rtcInit && serviceIsActive(&g_rtcSrv)) {
        struct {
            u32 device_code;
            u32 rtc_alarm_id;
        } dis_in = { RTC_DEVICE_CODE, RTC_ALARM_ID };
        serviceDispatchIn(&g_rtcSrv, 11, dis_in);

        u64 rtc_now = 0;
        const u32 dev = RTC_DEVICE_CODE;
        if (R_FAILED(serviceDispatchInOut(&g_rtcSrv, 0, dev, rtc_now))) {
            timeGetCurrentTime(TimeType_UserSystemClock, &rtc_now);
        }

        if (rtc_now != 0) {
            struct {
                u32 device_code;
                u32 rtc_alarm_id;
                u64 alarm_time_seconds;
            } in = { RTC_DEVICE_CODE, RTC_ALARM_ID, rtc_now + (u64)delay_seconds };

            serviceDispatchIn(&g_rtcSrv, 10, in);
        }
    }

    return 0;
}

Result alarmHelperCancel(void) {
    if (serviceIsActive(&g_alarmSubSrv)) {
        serviceDispatch(&g_alarmSubSrv, 2);
        serviceClose(&g_alarmSubSrv);
        memset(&g_alarmSubSrv, 0, sizeof(Service));
    }

    if (g_rtcInit && serviceIsActive(&g_rtcSrv)) {
        struct {
            u32 device_code;
            u32 rtc_alarm_id;
        } in = { RTC_DEVICE_CODE, RTC_ALARM_ID };

        serviceDispatchIn(&g_rtcSrv, 11, in);
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
