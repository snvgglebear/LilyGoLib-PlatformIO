#pragma once

/**
 * @file      settings_screen.h
 * @license   MIT
 * @brief     The settings page: an lv_menu on its own lv_screen_load()-able
 *            screen, modelled on src/factory/ui_sys.cpp.
 *
 * Its own screen rather than another tab on the Gadgetbridge tabview,
 * deliberately: settings should not be something the user lands on by swiping
 * past Music, and a screen keeps left/right free for the page's own use later.
 * It is reachable from exactly two places -- the Gadgetbridge launcher grid and
 * the quick-settings tray's gear.
 */

#include <lvgl.h>

/// Create the (empty) screen. Call once from setupGui(), after
/// usable_area_init(); the contents are built per-visit by
/// settings_screen_open().
void settings_screen_init(void);

/**
 * Build the menu with current values and load the screen.
 *
 * Rebuilt every visit rather than kept: brightness in particular can have been
 * changed from the quick-settings tray since the last time this was open, and
 * ~50 objects is not a cost worth caching against that.
 *
 * Remembers whichever screen is active now, and returns to it on exit.
 */
void settings_screen_open(void);
