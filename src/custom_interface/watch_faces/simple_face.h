#pragma once

#include <lvgl.h>

/**
 * @file      simple_face.h
 * @license   MIT
 * @brief     Digital watch face -- time, date, battery and charge state --
 *            laid out after src/factory's setupClock() (ui_main.cpp) for use
 *            as an installable face in custom_interface.
 */

/**
 * The three Alibaba PuHuiTi Bold cuts the face is set in, compiled from
 * font_alibaba_{100,24,12}.c alongside this header.
 *
 * Declared here rather than in the .cpp because app_config.h's APP_FONT_FACE_*
 * macros expand to these symbols: that header is deliberately free of an
 * <lvgl.h> dependency and so cannot declare a font itself, and this is the
 * header that every consumer of those macros already includes.
 */
LV_FONT_DECLARE(font_alibaba_100);
LV_FONT_DECLARE(font_alibaba_24);
LV_FONT_DECLARE(font_alibaba_12);

/*img_battery, the outline the meter is drawn inside, comes from here -- it
  moved to the shared image set when the Wi-Fi port brought factory's icons
  across, so it is no longer declared in this header.*/
#include "../images/images.h"

/// Builds the face inside usable_area_rect(screen) and starts its 1 Hz refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
void simple_face_init(lv_obj_t *screen);

/// Stop the refresh timer and delete the face's widgets, leaving @p screen as
/// it was before simple_face_init(). Safe to call when nothing was built.
void simple_face_deinit(void);
