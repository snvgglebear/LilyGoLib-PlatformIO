#pragma once
#include <stdint.h>
#include <lvgl.h>

/**
 * @file      batman_dial.h
 * @license   MIT
 * @brief     Analog watch face with drawn hands (hour/minute/second lines
 *            plus a digital readout), ported from
 *            examples/ui/BatmanDial/BatmanDial.ino for use as an installable
 *            face in custom_interface.
 */

/// Builds the dial inside usable_area_rect(screen) and starts its 1 Hz refresh
/// timer. Call once, after usable_area_init() has styled/clipped @p screen.
#define small_tick_length   6
#define large_tick_length   15
#define ARC_SPACING         30
#define ARC_WIDTH           8
#define ARC_GAP             8
#define HOUR_HAND_WIDTH     8
#define MIN_HAND_WIDTH      4
#define SEC_HAND_WIDTH      2
#define HOUR_HAND_COLOR     lv_palette_main(LV_PALETTE_RED)
#define MIN_HAND_COLOR      lv_color_white()
#define SEC_HAND_COLOR      lv_palette_main(LV_PALETTE_YELLOW)
void batman_dial_init(lv_obj_t *screen);
