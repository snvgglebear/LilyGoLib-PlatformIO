/**
 * @file      ui_notification_popup.h
 * @license   MIT
 * @brief     Auto-dismissing toast for newly arrived Gadgetbridge notifications.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/// Register the app_gadgetbridge.h listeners: one raises a toast for each
/// newly arrived notification (GB_CHANGE_NOTIFICATIONS), the other applies
/// phone-driven settings updates (GB_CHANGE_SETTINGS). Call once from
/// setupGui(), after app_gb_init() -- both new_interface.ino and main.cpp
/// already run app_gb_init() before setupGui(), so this is safe to call
/// anywhere inside it. Also seeds the timeout/vibrate state from NVS and
/// reports it to GbApp so the connect-time settings echo is correct.
void ui_notification_popup_init(void);

/// How long a toast stays up before auto-dismissing, and whether it vibrates
/// on arrival -- both persisted to NVS (user_setting_params_t, hal_interface.h)
/// and editable from ui_sys.cpp's Notifications settings row.
void ui_notification_popup_set_timeout_ms(uint32_t ms);
void ui_notification_popup_set_vibrate(bool enable);

/// Current values, for ui_sys.cpp to initialise its slider/switch from.
uint32_t ui_notification_popup_get_timeout_ms(void);
bool ui_notification_popup_get_vibrate(void);

/// True while a toast is currently on screen. Used by ui_boot_button.cpp's
/// short-press handler to dismiss the toast instead of navigating home.
bool ui_notification_popup_is_showing(void);

/// Dismiss the current toast immediately (same effect as tapping it or
/// letting its timeout elapse). Safe to call when no toast is showing.
void ui_notification_popup_dismiss(void);
