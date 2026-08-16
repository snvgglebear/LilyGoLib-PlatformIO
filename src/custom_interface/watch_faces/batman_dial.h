#pragma once

#include <lvgl.h>

/**
 * @file      batman_dial.h
 * @license   MIT
 * @brief     Analog watch face with drawn hands (hour/minute/second lines
 *            plus a digital readout), ported from
 *            examples/ui/BatmanDial/BatmanDial.ino for use as an installable
 *            face in custom_interface.
 */

/// Builds the dial inside safe_area_rect(screen) and starts its 1 Hz refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
void batman_dial_init(lv_obj_t *screen);
