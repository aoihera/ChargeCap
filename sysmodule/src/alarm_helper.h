/*
 * Alarm Helper Header for ChargeCap
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <switch.h>

Result alarmHelperInit(void);
void   alarmHelperExit(void);

Result alarmHelperSchedule(u32 delay_seconds);
Result alarmHelperCancel(void);
bool   alarmHelperIsScheduled(u32 *out_remaining_seconds);
