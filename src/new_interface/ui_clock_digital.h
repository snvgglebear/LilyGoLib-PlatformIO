/**
 * @file      ui_clock_digital.h
 * @license   MIT
 * @brief     Digital clock face for the clockface screen.
 */
#pragma once

#include <lvgl.h>

/// Build the digital clock face in `parent` (typically the container
/// usable_area_place() returned for ui_clockface_build()'s full-screen band).
/// Self-manages a 1 Hz lv_timer_t that refreshes it from hw_get_date_time();
/// no external tick call is needed.
lv_obj_t *ui_clock_digital_create(lv_obj_t *parent);

/// Stop the refresh timer and delete the widget tree returned by
/// ui_clock_digital_create(). Safe to call with NULL.
void ui_clock_digital_destroy(lv_obj_t *obj);
