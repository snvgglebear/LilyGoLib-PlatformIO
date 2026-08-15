/**
 * @file      ui_clockface.h
 * @license   MIT
 * @brief     Clockface screen: full-screen digital or analog clock, tap to
 *            toggle between them.
 */
#pragma once

#include <lvgl.h>

/// Build the clockface in `parent` (ui_main.cpp's clock_panel, tile (0,0)):
/// the clock, digital or analog per the persisted mode, sized to the whole
/// safe area -- tap anywhere on it to toggle. Called once from setupGui();
/// the returned widgets live for the process's entire lifetime (this tile is
/// never destroyed), so there is no matching destroy function.
void ui_clockface_build(lv_obj_t *parent);
