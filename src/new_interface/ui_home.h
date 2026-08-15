/**
 * @file      ui_home.h
 * @license   MIT
 * @brief     Home screen: clock, battery, pinned links.
 */
#pragma once

#include <lvgl.h>

/// Build the home screen inside `parent` (ui_main.cpp's menu_panel, tile
/// (0,0)). Safe-area-aware: every widget is placed through
/// usable_area/usable_area.h rather than raw coordinates. Top to bottom:
///   - the clock, digital or analog per the persisted mode -- tap to toggle;
///   - ui_battery_status_create();
///   - ui_pinned_links_build().
/// Called once from setupGui(); the returned widgets live for the process's
/// entire lifetime (tile (0,0) is never destroyed), so there is no matching
/// destroy function.
void ui_home_build(lv_obj_t *parent);
