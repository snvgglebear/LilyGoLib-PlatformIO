/**
 * @file      gb_ui.h
 * @license   MIT
 * @brief     LVGL front end for the Gadgetbridge app, laid out with
 *            custom_interface's safe-area helpers (usable_area.h)
 *            so nothing sits under the T-Watch-Ultra's curved bezel.
 *
 * Builds the screens and keeps them in step with GbApp. Ported from
 * src/gadgetbridge/gb_ui.cpp, which lays out the same screens directly against
 * lv_screen_active() -- fine on that app's flat-screen boards, but this app
 * targets the curved Ultra panel, so every screen here is built inside
 * usable_area_rect()/usable_area_place() instead.
 */
#pragma once

#include <lvgl.h>

#include "gb_app.h"

/// Build the screens onto @p screen. Call after usable_area_init() (or, if
/// @p screen isn't the boot screen, usable_area_style_screen(screen)) and
/// before gb_app.begin().
void gb_ui_begin(lv_obj_t *screen);

/// Return the screen to its launcher grid, so entering it always lands on the
/// grid rather than wherever the user last left off. Closes the full-screen
/// conversation view if one is open -- that lives on lv_layer_top() and would
/// otherwise stay stranded over the grid. Safe before gb_ui_begin() (no-op).
void gb_ui_show_home(void);

/// True when the launcher grid is the visible page and nothing full-screen is
/// layered over it -- i.e. a "back"/"home" press has nothing left to back out
/// of here and should leave the Gadgetbridge screen entirely. False before
/// gb_ui_begin().
bool gb_ui_at_home(void);

/// Listener handed to GbApp::begin(); refreshes whatever the change affected.
void gb_ui_on_state_changed(GbStateChange change);
