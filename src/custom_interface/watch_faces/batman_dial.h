#pragma once
#include <stdint.h>
#include <lvgl.h>

/**
 * @file      batman_dial.h
 * @license   MIT
 * @brief     Analog watch face with drawn hands (hour/minute/second lines
 *            plus a battery arc), ported from
 *            examples/ui/BatmanDial/BatmanDial.ino for use as an installable
 *            face in custom_interface.
 *
 * Every tunable the face has lives below, so the look can be changed without
 * touching batman_dial.cpp. Sizes are in px unless the name says otherwise.
 */

// ---------------------------------------------------------------------------
// Face geometry
// ---------------------------------------------------------------------------
/// Kept clear inside the panel (or usable area) before anything is drawn.
#define FACE_MARGIN         10
/// Radial room reserved OUTSIDE the dial for the battery arc and its readout.
/// Widening it shrinks the dial and everything sized off it. Must be at least
/// ARC_GAP + ARC_WIDTH for the arc to fit in the space it is given.
#define ARC_SPACING         20

#define small_tick_length   6
#define large_tick_length   15
#define SMALL_TICK_WIDTH    2
#define LARGE_TICK_WIDTH    3
/// 61 ticks over 360 deg is exactly 6 deg apart. 60 would space them 360/59
/// and the ring would visibly fail to close -- see build_face().
#define TICK_COUNT          61
#define MAJOR_TICK_EVERY    5

/// Gap between the inner end of a major tick and the hour numeral below it.
/// A real knob only because the numerals are placed by hand -- lv_scale's own
/// label distance is a #define inside lv_scale.c and is not stylable.
#define NUMERAL_GAP         12

// ---------------------------------------------------------------------------
// Hands -- lengths as a percentage of the dial radius
// ---------------------------------------------------------------------------
#define HOUR_HAND_PCT       50
#define MIN_HAND_PCT        75
#define SEC_HAND_PCT        90
#define HOUR_HAND_WIDTH     10
#define MIN_HAND_WIDTH      4
#define SEC_HAND_WIDTH      2
#define HOUR_HAND_COLOR     lv_palette_main(LV_PALETTE_RED)
#define MIN_HAND_COLOR      lv_color_white()
#define SEC_HAND_COLOR      lv_palette_main(LV_PALETTE_YELLOW)

// ---------------------------------------------------------------------------
// Battery arc
// ---------------------------------------------------------------------------
#define ARC_WIDTH           8
/// Dial edge -> arc, and arc -> readout.
#define ARC_GAP             8
/// LVGL angles: 0 deg is 3 o'clock and grows clockwise, so 270..360 is the
/// top-right quadrant. The readout is placed at the midpoint of the two.
#define ARC_START_DEG       270
#define ARC_END_DEG         360
#define ARC_MIN             0
#define ARC_MAX             100

// ---------------------------------------------------------------------------
// Fonts and colors
// ---------------------------------------------------------------------------
#define NUMERAL_FONT        &lv_font_montserrat_28
#define READOUT_FONT        &lv_font_montserrat_20
#define NUMERAL_COLOR       lv_color_white()
#define READOUT_COLOR       lv_color_white()
#define DIAL_BG_COLOR       lv_color_hex(0x101010)
#define DIAL_BORDER_COLOR   lv_palette_main(LV_PALETTE_YELLOW)
#define DIAL_BORDER_WIDTH   3
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
