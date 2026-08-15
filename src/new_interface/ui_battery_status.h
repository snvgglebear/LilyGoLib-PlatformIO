/**
 * @file      ui_battery_status.h
 * @license   MIT
 * @brief     Battery percent + icon widget for the home screen.
 */
#pragma once

#include <lvgl.h>

/// Build the battery status widget (icon + "NN%" label) in `parent`. Polls
/// hw_get_monitor_params() on its own lv_timer_t at BATTERY_POLL_INTERVAL_MS
/// (app_config.h) for as long as the process runs -- the widget is part of
/// the permanent home screen and is never torn down, so it has no destroy
/// function. Also raises a dismissable low-battery warning (create_msgbox()
/// on lv_layer_top()) the first time the percentage drops to the configured
/// threshold (loaded from NVS, default LOW_BATTERY_WARNING_PERCENT) or below,
/// re-arming once it rises back above LOW_BATTERY_WARNING_REARM_PERCENT -- in
/// addition to, not instead of, ui_main.cpp's hw_device_poll() hard shutdown.
lv_obj_t *ui_battery_status_create(lv_obj_t *parent);
