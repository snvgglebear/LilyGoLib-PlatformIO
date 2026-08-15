/**
 * @file      ui_alarms.h
 * @license   MIT
 * @brief     Alarms / timer / stopwatch app (src/custom_interface/plan.md:
 *            "alarms, timers, and stopwatch features").
 *
 * Three views inside one app_t, switched with a small segmented control:
 *   - Alarms:    read + acknowledge only. Alarms themselves are phone-set
 *                (protocol §5.8) and arrive through GbApp; this screen never
 *                creates or edits one locally.
 *   - Timer:     a local countdown, adjustable before starting.
 *   - Stopwatch: a local count-up, with an optional lap list.
 *
 * @see ui_alarms.cpp for the implementation and the design notes at its top.
 */
#pragma once

#include "ui_define.h"

/// The launchable app -- register with create_app() in ui_main.cpp (done by a
/// separate integration step, not this file).
extern app_t ui_alarms_main;

/// Register this app's gadgetbridge listener (GB_CHANGE_ALARMS refreshes the
/// Alarms list; GB_CHANGE_ALARM_FIRED raises/dismisses the fired-alarm overlay
/// on lv_layer_top(), visible regardless of which app is currently open).
/// Call once, after app_gb_init(), before any alarm can plausibly fire --
/// setupGui() satisfies this ordering. Safe to call even if no alarm is set.
void ui_alarms_init(void);
