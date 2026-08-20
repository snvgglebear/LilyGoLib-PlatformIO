#pragma once
#include <stdint.h>
#include <lvgl.h>

#include "../app_config.h"

/**
 * @file      batman_dial.h
 * @license   MIT
 * @brief     Analog watch face with drawn hands (hour/minute/second lines
 *            plus a battery arc), ported from
 *            examples/ui/BatmanDial/BatmanDial.ino for use as an installable
 *            face in custom_interface.
 *
 * The face's colors and the two pieces of geometry that are counts rather
 * than sizes live below. Everything with a px, a percentage or a font in it
 * -- margins, tick lengths, hand widths, the arc, the numeral and readout
 * fonts -- is in ../app_config.h under "Analog dial face", with the rest of
 * the app's sizes.
 */

// ---------------------------------------------------------------------------
// Tick ring
// ---------------------------------------------------------------------------
/// 61 ticks over 360 deg is exactly 6 deg apart. 60 would space them 360/59
/// and the ring would visibly fail to close -- see build_face().
#define TICK_COUNT          61
#define MAJOR_TICK_EVERY    5

// ---------------------------------------------------------------------------
// Hand colors
// ---------------------------------------------------------------------------
#define HOUR_HAND_COLOR     lv_palette_main(LV_PALETTE_RED)
#define MIN_HAND_COLOR      lv_color_white()
#define SEC_HAND_COLOR      lv_palette_main(LV_PALETTE_YELLOW)

// ---------------------------------------------------------------------------
// Battery arc
// ---------------------------------------------------------------------------
/// LVGL angles: 0 deg is 3 o'clock and grows clockwise, so 270..360 is the
/// top-right quadrant. The readout is placed at the midpoint of the two.
#define ARC_START_DEG       270
#define ARC_END_DEG         360
#define ARC_MIN             0
#define ARC_MAX             100

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define NUMERAL_COLOR       lv_color_white()
#define READOUT_COLOR       lv_color_white()
#define DIAL_BG_COLOR       lv_color_hex(0x101010)
#define DIAL_BORDER_COLOR   lv_palette_main(LV_PALETTE_YELLOW)
#define SMALL_TICK_COLOR    lv_palette_main(LV_PALETTE_GREY)
#define LARGE_TICK_COLOR    lv_palette_main(LV_PALETTE_YELLOW)
/// Filled portion of the battery arc, and the unfilled track behind it.
#define ARC_COLOR           lv_palette_main(LV_PALETTE_YELLOW)
#define ARC_TRACK_COLOR     lv_color_hex(0x303030)

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
/// The second hand only needs to jump once a second, not redraw continuously.
#define REFRESH_INTERVAL_MS 1000

/// Builds the dial inside usable_area_rect(screen) and starts its refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
void batman_dial_init(lv_obj_t *screen);

/// Stop the refresh timer and delete the dial's widgets, leaving the screen as
/// it was before batman_dial_init(). Safe to call when nothing was built.
void batman_dial_deinit(void);
