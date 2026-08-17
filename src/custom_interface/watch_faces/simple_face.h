#pragma once

#include <lvgl.h>

/**
 * @file      simple_face.h
 * @license   MIT
 * @brief     Minimal watch face -- time, date, battery and charge state --
 *            ported from examples/ui/SimpleWatch/SimpleWatch.ino for use as
 *            an installable face in custom_interface.
 */

/// Builds the face inside usable_area_rect(screen) and starts its 1 Hz refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
void simple_face_init(lv_obj_t *screen);
