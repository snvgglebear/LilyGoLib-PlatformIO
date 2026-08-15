/**
 * @file      app_config.h
 * @license   MIT
 * @brief     Single source of truth for new_interface's own constants and
 *            default settings.
 *
 * Per src/custom_interface/plan.md: "All constants and configuration options
 * should be defined in a single location to allow for easy modification."
 * This file holds the knobs specific to the home screen / notification /
 * battery / pinned-links / alarm features added on top of the factory demo
 * and the ported gadgetbridge_ble module. Board pin-outs, radio defaults,
 * and the rest of the factory app's own constants stay in hal_interface.h
 * (unchanged) -- this file is additive, not a replacement for it.
 */
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Clock face
// ---------------------------------------------------------------------------

/// Which clock face is shown by default, before the user has picked one.
/// Persisted at runtime in app_state_t::clock_mode (see below).
enum ClockMode {
    CLOCK_MODE_DIGITAL,
    CLOCK_MODE_ANALOG,
};
#define CLOCK_MODE_DEFAULT   CLOCK_MODE_DIGITAL

// ---------------------------------------------------------------------------
// Battery status / low-battery warning
// ---------------------------------------------------------------------------

/// Percentage at which ui_battery_status.cpp raises a dismissable warning
/// popup. Deliberately well above the hard-shutdown floor in ui_main.cpp's
/// hw_device_poll() (3300 mV, ~single-digit percent) so the user gets real
/// notice before the firmware protects the cell by powering off.
#define LOW_BATTERY_WARNING_PERCENT   20

/// Once shown, the warning will not fire again until the percentage rises
/// back above this (re-arm) threshold -- prevents it re-popping every poll
/// while sitting just under LOW_BATTERY_WARNING_PERCENT.
#define LOW_BATTERY_WARNING_REARM_PERCENT   25

/// How often the battery is polled for the warning check.
#define BATTERY_POLL_INTERVAL_MS   15000

// ---------------------------------------------------------------------------
// Notification popups (toasts)
// ---------------------------------------------------------------------------

/// Default time a notification toast stays on screen before auto-dismissing.
#define NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS   6000

/// Default vibrate-on-arrival setting.
#define NOTIFICATION_POPUP_DEFAULT_VIBRATE      true

/// Bounds for the user-facing timeout slider (ui_home.cpp's settings row).
#define NOTIFICATION_POPUP_MIN_TIMEOUT_MS       2000
#define NOTIFICATION_POPUP_MAX_TIMEOUT_MS       15000

// ---------------------------------------------------------------------------
// Pinned links (home screen shortcuts)
// ---------------------------------------------------------------------------

/// One bit per pinnable app. Keep in sync with the create_app() calls these
/// gate in ui_main.cpp / ui_pinned_links.cpp -- this is the single place that
/// numbers them.
enum PinnableApp {
    PIN_WIRELESS = 0,
    PIN_ALARMS,
    PIN_LORA,
    PIN_IR_REMOTE,
    PIN_GPS,
    PIN_MONITOR,
    PIN_SETTINGS,
    PIN_APP_COUNT
};

/// Default pinned set, shown on first boot / after a settings reset.
#define PINNED_APPS_DEFAULT_MASK  \
    ((1u << PIN_WIRELESS) | (1u << PIN_ALARMS) | (1u << PIN_LORA) | (1u << PIN_SETTINGS))

/// Max apps shown in the pinned row before the user has to open "All Apps".
#define PINNED_APPS_MAX_VISIBLE   4

// ---------------------------------------------------------------------------
// LoRa radio on/off
// ---------------------------------------------------------------------------

/// Whether the LoRa radio (SX1262/SX1280/LR1121/CC1101, whichever is compiled
/// in) is allowed to run at all, before any per-app configuration. Off by
/// default -- fail closed, matching this repo's Meshtastic-node regulatory
/// posture (src/new_interface/plans/lora-meshtastic-protocol-interop-plan.md
/// §5.6/§10) even before that module exists: ui_radio.cpp and ui_msgchat.cpp
/// already respect this flag today. See hal_interface.h's
/// hw_get_lora_enabled()/hw_set_lora_enabled().
#define LORA_RADIO_DEFAULT_ENABLED   false

// ---------------------------------------------------------------------------
// Alarms / timer / stopwatch
// ---------------------------------------------------------------------------

/// How often ui_alarms.cpp re-checks the countdown timer / stopwatch display.
#define ALARM_UI_REFRESH_MS   500

/// Default countdown timer length offered when the screen is first opened.
#define TIMER_DEFAULT_SECONDS   (5 * 60)

// ---------------------------------------------------------------------------
// IR remote -- persisted multi-code list (plans/ir-remote-learn-mode-plan.md)
// ---------------------------------------------------------------------------

/// Max length of a user-entered code label / device tag, including NUL.
#define IR_CODE_LABEL_MAX_LEN   24
/// Hard cap on saved codes; sized against the 20KB nvs partition (§3.2).
#define IR_CODE_MAX_ENTRIES     32

/// Format version of ir_code_list_t (plans/ir-remote-learn-mode-plan.md §3.3).
#define IR_CODE_LIST_VERSION    1

/// One saved code. `value` is uint64_t to match hw_get_remote_code()'s width;
/// `protocol` is the IRremoteESP8266 decode_type_t (or a small local enum),
/// `bits` the decoded bit length (32 for legacy NEC/manual entries).
typedef struct {
    char     label[IR_CODE_LABEL_MAX_LEN];  ///< user-entered name, NUL-terminated
    char     device[IR_CODE_LABEL_MAX_LEN]; ///< optional grouping tag, "" = ungrouped
    uint8_t  protocol;                      ///< decode type / protocol id
    uint8_t  bits;                          ///< decoded bit length
    uint64_t value;                         ///< the IR code itself
} ir_code_entry_t;

/// Whole persisted list, stored as one NVS blob under the "ir_codes" namespace.
typedef struct {
    uint8_t version;              ///< IR_CODE_LIST_VERSION; mismatch => reset
    uint8_t count;
    ir_code_entry_t entries[IR_CODE_MAX_ENTRIES];
} ir_code_list_t;
