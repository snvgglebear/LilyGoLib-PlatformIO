/**
 * @file      ui_clock_analog.h
 * @license   MIT
 * @brief     Analog clock face for the clockface screen.
 */
#pragma once

#include <lvgl.h>

/// Build the analog clock face (drawn hour/minute/second hands, ported from
/// examples/ui/BatmanDial/BatmanDial.ino) in `parent`. Self-manages a 1 Hz
/// lv_timer_t that repositions the hands from hw_get_date_time(); no
/// external tick call is needed.
lv_obj_t *ui_clock_analog_create(lv_obj_t *parent);

/// Stop the refresh timer and delete the widget tree returned by
/// ui_clock_analog_create(). Safe to call with NULL.
void ui_clock_analog_destroy(lv_obj_t *obj);
