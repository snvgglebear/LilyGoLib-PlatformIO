#pragma once

#include <lvgl.h>

/**
 * @file      simple_face.h
 * @license   MIT
 * @brief     Minimal watch face -- time, date, battery and charge state --
 *            ported from examples/ui/SimpleWatch/SimpleWatch.ino for use as
 *            an installable face in custom_interface.
 */
LV_FONT_DECLARE(font_clock_120);
#define APP_FONT_FACE_TIME &font_clock_120
/// Builds the face inside usable_area_rect(screen) and starts its 1 Hz refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
void simple_face_init(lv_obj_t *screen);

/// Stop the refresh timer and delete the face's widgets, leaving @p screen as
/// it was before simple_face_init(). Safe to call when nothing was built.
void simple_face_deinit(void);
