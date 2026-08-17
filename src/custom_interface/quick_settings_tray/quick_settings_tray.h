#pragma once

/**
 * @file      quick_settings_tray.h
 * @license   MIT
 * @brief     Swipe-down quick-settings shade: time/date, battery, brightness.
 *            See ../../.claude/swipe-down-quick-settings-tray-plan.md.
 *
 * Lives on lv_layer_top(), independent of whichever screen
 * (lv_screen_load()) is active, so one init() covers every screen the app
 * ever switches to -- open() just needs calling from the right place.
 *
 * The object tree is routed through usable_area.h's curved-bezel
 * engine, which lays out against the Ultra's geometry and degrades
 * to plain full-width containers on the flat-panel boards.
 */

#include <lvgl.h>

/// Build the scrim + tray on lv_layer_top(), hidden. Call once, after
/// usable_area_init() (needs usable_area_screen_width/height()).
void quick_settings_tray_init(void);

/// Slide the tray in over whatever screen is active and dim it. Call this
/// from a gesture handler on any non-watchface screen -- never from the
/// watchface's, which owns swipe-down for its own navigation.
void quick_settings_tray_open(void);
