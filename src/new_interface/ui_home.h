/**
 * @file      ui_home.h
 * @license   MIT
 * @brief     Home screen: battery status, pinned links.
 */
#pragma once

#include <lvgl.h>

/// Build the home screen inside `parent` (ui_main.cpp's menu_panel). Safe-
/// area-aware: every widget is placed through usable_area.h
/// rather than raw coordinates. Top to bottom:
///   - ui_battery_status_create();
///   - ui_pinned_links_build().
/// The clock lives on its own tile now (ui_clockface.cpp's clock_panel,
/// reached by swiping to/from this screen or the boot button's short press --
/// see src/custom_interface/plan.md's interface_bugfixes and
/// plans/boot-button-input-plan.md). Called once from setupGui(); the
/// returned widgets live for the process's entire lifetime (this tile is
/// never destroyed), so there is no matching destroy function.
void ui_home_build(lv_obj_t *parent);
