/**
 * @file      gb_ui_metrics.h
 * @license   MIT
 * @brief     Tap-target sizes for the Gadgetbridge UI, defined once and
 *            referenced everywhere gb_ui.cpp builds a button or the tab bar.
 *
 * LVGL's theme sizes buttons for a mouse/stylus; this app is touched with a
 * fingertip on a wrist-worn screen, so every button and the tab bar reads
 * its size from here instead of the theme default. To resize the UI, change
 * a value here rather than hunting through gb_ui.cpp.
 */
#pragma once

#include <stdint.h>

/// Height of a standalone content button: "Ring my phone", "Dismiss all",
/// thread view back/reply/dismiss, music transport/volume. Width stays
/// content-sized (the label plus padding), floored by GB_BUTTON_MIN_WIDTH.
constexpr int32_t GB_BUTTON_HEIGHT = 50;

/// Minimum width for a content button, so an icon-only button (e.g. the
/// music transport symbols) still gets a reasonably sized tap target.
constexpr int32_t GB_BUTTON_MIN_WIDTH = 64;

/// Height of a msgbox header/footer strip -- close, back, and the action
/// buttons on it are LV_PCT(100) of this, so setting it sizes them too.
constexpr int32_t GB_MSGBOX_STRIP_HEIGHT = 50;

/// Bottom tab bar height (Watch/Chats/Alerts/Music), by panel size.
constexpr int32_t GB_TAB_BAR_HEIGHT_LARGE = 48;  ///< T-Watch Ultra (410x502)
constexpr int32_t GB_TAB_BAR_HEIGHT_SMALL = 34;  ///< T-Watch S3 (240x240)
