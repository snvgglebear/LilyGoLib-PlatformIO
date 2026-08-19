#pragma once

/**
 * @file      app_config.h
 * @license   MIT
 * @brief     Every user-adjustable default and range in custom_interface, in
 *            one place.
 *
 * The settings page (settings/settings_screen.cpp) reads its slider bounds
 * from here and the settings store (settings/app_settings.cpp) reads its
 * defaults from here, so "what does this reset to" and "how far can it go"
 * are answered by one file rather than by two that have to agree.
 *
 * Layout constants that only one module cares about stay with that module --
 * gb_ui_metrics.h for the Gadgetbridge screen, batman_dial.h for the dial.
 * This header is for values the *user* can change.
 */

#include <stdint.h>

// ---------------------------------------------------------------------------
// Display & backlight
// ---------------------------------------------------------------------------
/// Boot/restore-defaults brightness, as a percentage of the board's own
/// qst_hal_brightness_min()..max() span. A percentage rather than a raw level
/// because that span is board dependent (0-255 on the Ultra).
constexpr uint8_t APP_BRIGHTNESS_DEFAULT_PCT = 60;

/// Idle timeout before the display sleeps. 0 means never, and the settings
/// slider uses APP_SCREEN_TIMEOUT_MIN_S as its "never" stop rather than
/// leaving a dead zone at the low end.
constexpr uint16_t APP_SCREEN_TIMEOUT_DEFAULT_S = 10;
constexpr uint16_t APP_SCREEN_TIMEOUT_MIN_S     = 0;    ///< 0 = never sleep
constexpr uint16_t APP_SCREEN_TIMEOUT_MAX_S     = 180;
constexpr uint16_t APP_SCREEN_TIMEOUT_STEP_S    = 5;

/// Raise-to-wake, on the BHI260AP boards only (HAS_WRIST_TILT_SENSOR).
constexpr bool APP_WRIST_WAKE_DEFAULT = true;

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------
/// How long a "new message" popup stays up before dismissing itself.
/// Bounds copied from src/new_interface/app_config.h, which tuned them.
constexpr uint16_t APP_NOTIF_POPUP_DEFAULT_MS = 6000;
constexpr uint16_t APP_NOTIF_POPUP_MIN_MS     = 2000;
constexpr uint16_t APP_NOTIF_POPUP_MAX_MS     = 15000;

/// Haptics, split by what the buzz is for -- see gb_app.cpp's vibrateFor().
constexpr bool APP_VIBRATE_MESSAGES_DEFAULT = true;   ///< GB_HAPTIC_TAP
constexpr bool APP_VIBRATE_ALERTS_DEFAULT   = true;   ///< GB_HAPTIC_ALERT

// ---------------------------------------------------------------------------
// Watch face
// ---------------------------------------------------------------------------
/// Which face setupGui() builds on screen_home. Values are WatchFaceId
/// (watch_faces/face_registry.h); kept as a plain int here so app_config.h
/// stays dependency-free.
constexpr uint8_t APP_WATCH_FACE_DEFAULT = 1;   ///< WATCH_FACE_ANALOG

// ---------------------------------------------------------------------------
// Settings page layout
// ---------------------------------------------------------------------------
/// Height of one settings row. Well above a fingertip, because these rows are
/// scrolled past as often as they are aimed at.
constexpr int32_t APP_SETTINGS_ROW_HEIGHT = 56;
