/**
 * @file      gb_ui.h
 * @license   MIT
 * @brief     LVGL front end for the Gadgetbridge app.
 *
 * Builds the screens and keeps them in step with GbApp. The same code runs on
 * hardware and in the native/SDL2 emulator -- it only ever touches LVGL and the
 * GbApp accessors, never the board directly.
 */
#pragma once

#include "gb_app.h"

/// Build the screens. Call after LVGL is up and before gb_app.begin().
void gb_ui_begin();

/// Listener handed to GbApp::begin(); refreshes whatever the change affected.
void gb_ui_on_state_changed(GbStateChange change);
