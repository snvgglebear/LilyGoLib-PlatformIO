/**
 * @file      gb_ui_metrics.h
 * @license   MIT
 * @brief     Tap-target sizes for the Gadgetbridge UI, defined once and
 *            referenced everywhere gb_ui.cpp builds a button, the status bar
 *            or the launcher grid.
 *
 * LVGL's theme sizes buttons for a mouse/stylus; this app is touched with a
 * fingertip on a wrist-worn screen, so every button reads its size from here
 * instead of the theme default. To resize the UI, change a value here rather
 * than hunting through gb_ui.cpp.
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

/// Status bar height, by panel size. Sized to hold the home button, not just
/// the two labels -- see GB_STATUS_BUTTON_SIZE_*.
constexpr int32_t GB_STATUS_BAR_HEIGHT_LARGE = 40;  ///< T-Watch Ultra (410x502)
constexpr int32_t GB_STATUS_BAR_HEIGHT_SMALL = 24;  ///< T-Watch S3 (240x240)

/// Square side of the status bar's back-to-grid button. Smaller than
/// GB_BUTTON_HEIGHT because it shares a strip with the link/battery labels;
/// GB_STATUS_BUTTON_EXT_CLICK makes the *tappable* area finger-sized.
constexpr int32_t GB_STATUS_BUTTON_SIZE_LARGE = 34;
constexpr int32_t GB_STATUS_BUTTON_SIZE_SMALL = 22;
constexpr int32_t GB_STATUS_BUTTON_EXT_CLICK  = 10;  ///< invisible tap margin

/// Launcher grid: column and row counts, and the gap/padding around its tiles.
/// Five entries in a 2x3 leaves the bottom-right cell empty; on the S3 that
/// puts tiles at roughly 108x60, which is the tight case worth checking first
/// if a sixth is ever added.
constexpr int32_t GB_GRID_COLS = 2;
constexpr int32_t GB_GRID_ROWS = 3;
constexpr int32_t GB_GRID_GAP  = 10;
constexpr int32_t GB_GRID_PAD  = 8;

/// Whether a grid tile tap animates to its page. Off by default: an animated
/// scroll from the grid to Music sweeps *through* Watch/Chats/Alerts, i.e.
/// several full-screen redraws for a jump the user already committed to with
/// a tap. Swipes stay animated regardless -- that animation is lv_tabview's
/// own, driven by the finger.
constexpr bool GB_GRID_ANIMATE_TAB_CHANGE = false;
