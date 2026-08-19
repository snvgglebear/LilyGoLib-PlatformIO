#pragma once

/**
 * @file      app_settings.h
 * @license   MIT
 * @brief     The persisted user settings, and the one live copy of them.
 *
 * Modelled on src/factory/hal_interface.cpp's hw_get/set_user_setting(): one
 * packed struct written to NVS as a blob, with a version field so a layout
 * change is detected rather than misread as garbage. custom_interface has no
 * hal_interface layer, so the store is its own module instead.
 *
 * Usage is live-apply, commit once:
 *
 *   - app_settings_begin() loads (or defaults) and pushes every value into the
 *     subsystem that owns it, so boot honours what the user last chose;
 *   - a settings control changes the live copy, applies its own effect
 *     immediately, and calls app_settings_mark_dirty();
 *   - app_settings_flush() writes -- once, on the way out of the settings
 *     screen. Nothing writes NVS per event: a slider drag fires
 *     LV_EVENT_VALUE_CHANGED continuously and would hammer the flash.
 */

#include <stdint.h>

/// Bumped whenever AppSettings changes shape. A stored blob with a different
/// version (or a different size) is discarded in favour of the defaults.
constexpr uint16_t APP_SETTINGS_VERSION = 1;

struct AppSettings {
    uint16_t version;            ///< APP_SETTINGS_VERSION; mismatch -> defaults
    uint8_t  brightness;         ///< raw qst_hal level, board dependent range
    uint8_t  wrist_wake;
    uint16_t screen_timeout_s;   ///< 0 = never sleep
    uint8_t  watch_face;         ///< WatchFaceId
    uint8_t  vibrate_messages;
    uint8_t  vibrate_alerts;
    uint16_t notif_popup_ms;
};

/// The live copy. Read it freely; change it only through the setters below,
/// which keep the subsystem and the dirty flag in step.
const AppSettings &app_settings(void);

/// Load from NVS (or default) and apply everything. Call once from setupGui(),
/// before the watch face is built -- it decides which face that is.
void app_settings_begin(void);

/// Persist iff something changed since the last flush.
void app_settings_flush(void);

/// Reset to the app_config.h defaults, apply them live, and mark dirty. The
/// caller is expected to rebuild whatever UI is showing the old values.
void app_settings_restore_defaults(void);

/*Setters. Each one updates the live copy, pushes the value into the subsystem
  that owns it, and marks the store dirty -- so a control never has to remember
  to do all three, and "changed but not applied" cannot happen.*/
void app_settings_set_brightness(int level);
void app_settings_set_screen_timeout_s(uint16_t seconds);
void app_settings_set_wrist_wake(bool enable);
void app_settings_set_watch_face(uint8_t face);
void app_settings_set_notif_popup_ms(uint16_t ms);
void app_settings_set_vibrate_messages(bool enable);
void app_settings_set_vibrate_alerts(bool enable);
