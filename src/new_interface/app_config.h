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
#include <lvgl.h>

// ---------------------------------------------------------------------------
// Shared UI theme -- colors and common sizing
// ---------------------------------------------------------------------------
//
// Every ui_*.cpp screen builds its styling from these instead of calling
// lv_color_*()/lv_palette_*() or writing pixel literals directly, so the
// app's look can be retinted/resized by editing this section alone. Named by
// role, not value -- several roles deliberately share the same underlying
// color or size today but may need to diverge later.
//
// Base theme colors, also handed to lv_theme_default_init() in ui_main.cpp.
#define THEME_COLOR_BLACK                  lv_color_black()
#define THEME_COLOR_WHITE                  lv_color_white()
#define THEME_COLOR_PRIMARY                THEME_COLOR_BLACK
#define THEME_COLOR_SECONDARY              lv_palette_darken(LV_PALETTE_GREY, 3)

/// Whether lv_theme_default_init() runs LVGL's default theme in dark mode.
/// This is a deliberate `true`, NOT LVGL's own LV_THEME_DEFAULT_DARK config
/// macro -- that macro controls whether LVGL's bundled demos default to dark
/// mode, and LilyGoLib's shipped lv_conf.h (the only lv_conf.h in this
/// build's include path) hardcodes it to 0. Passing that macro through here
/// used to silently put every default-themed container (lv_obj_create(),
/// lv_list, parts of lv_menu) into LIGHT mode -- white/near-white
/// backgrounds with dark-grey text -- while the rest of this app assumes a
/// black background with white/light foreground elements throughout. Every
/// THEME_COLOR_TEXT_ON_DARK / THEME_COLOR_BG_DARK usage above depends on
/// this actually being dark.
#define THEME_USE_DARK_MODE                true

// Text
#define THEME_COLOR_TEXT_ON_DARK           THEME_COLOR_WHITE     ///< default text on this app's near-black screens
#define THEME_COLOR_TEXT_ON_LIGHT          THEME_COLOR_BLACK     ///< text on light/white cards (walkie, chat, clock, all-apps list)
#define THEME_COLOR_TEXT_SECONDARY         lv_palette_main(LV_PALETTE_GREY)     ///< de-emphasized captions (alarm status, toast body)
#define THEME_COLOR_TEXT_MUTED             lv_color_hex(0x888888)               ///< medium grey; walkie idle status label
#define THEME_COLOR_TEXT_INFO              lv_color_hex(0x5f5f5f)               ///< dark grey; walkie nickname/channel info rows
#define THEME_COLOR_TEXT_ON_LIGHT_DIM      lv_color_hex(0x2A2A2A)               ///< charcoal; walkie contact-list row text (same value as THEME_COLOR_BG_CHIP_DARK, different role -- kept separate on purpose)
#define THEME_COLOR_TEXT_PLACEHOLDER       lv_color_hex(0xAAAAAA)               ///< light grey; walkie "no messages yet" placeholder
#define THEME_COLOR_TEXT_WARNING           lv_palette_main(LV_PALETTE_ORANGE)   ///< settings screen "peripheral missing" note

// Backgrounds
#define THEME_COLOR_BG_DARK                THEME_COLOR_BLACK       ///< screen / dialog / menu backgrounds
#define THEME_COLOR_BG_LIGHT               THEME_COLOR_WHITE       ///< light "card" backgrounds (walkie, chat bar, toast buttons)
#define THEME_COLOR_BG_PANEL                lv_color_hex(0x1a1a1a)  ///< near-black; dark meter/level panel fill (microphone)
#define THEME_COLOR_BG_DIAL                 lv_color_hex(0x262626)  ///< dark grey; analog clock dial fill (kept clearly lighter than the black screen behind it for contrast)
#define THEME_COLOR_BG_TOAST               lv_color_hex(0x202020)  ///< very dark grey; notification toast
#define THEME_COLOR_BG_CHIP_DARK           lv_color_hex(0x2A2A2A)  ///< charcoal; walkie's neutral dark chip/button fill (and its inactive states)

// Borders
#define THEME_COLOR_BORDER_LIGHT           lv_color_hex(0xDDDDDD)               ///< pale grey; walkie contact-list panel border
#define THEME_COLOR_BORDER_NEUTRAL         lv_palette_main(LV_PALETTE_GREY)     ///< dropdown default border

// Accents
#define THEME_COLOR_ACCENT_RED             lv_color_hex(0xCC3333)               ///< brick red; walkie talking/recording indicator
#define THEME_COLOR_ACCENT_GREEN           lv_color_hex(0x33AA33)               ///< medium green; walkie connected-peer indicator
#define THEME_COLOR_ACCENT_BLUE            lv_palette_main(LV_PALETTE_BLUE)     ///< textarea focus outline, alarm tab active state
#define THEME_COLOR_ACCENT_YELLOW          lv_palette_main(LV_PALETTE_YELLOW)   ///< analog clock second hand + dial border
#define THEME_COLOR_ACCENT_PURPLE          lv_palette_main(LV_PALETTE_PURPLE)   ///< menu page border
#define THEME_COLOR_ALERT                  lv_palette_main(LV_PALETTE_RED)      ///< sensor motion LED

// Chat bubbles (ui_msgchat.cpp)
#define THEME_COLOR_CHAT_BUBBLE_SENT       lv_color_hex(0x99ccff)               ///< sky blue
#define THEME_COLOR_CHAT_BUBBLE_RECEIVED   lv_color_hex(0xe6e6e6)               ///< off-white / pale grey

/// Launcher / pinned-links icon "tile" background (ui_main.cpp's
/// create_app_icon()). Previously fully transparent (LV_OPA_0), which left
/// icons floating with no visible tile against the near-black screen --
/// bumped to a visible dark-grey fill for contrast.
#define THEME_COLOR_BG_TILE                lv_color_hex(0x2A2A2A)
#define THEME_TILE_BG_OPA                  LV_OPA_70

// Process-bar gradient (ui_tools.cpp's ui_create_process_bar())
#define THEME_COLOR_PROGRESS_GRADIENT_START   lv_palette_lighten(LV_PALETTE_GREY, 1)
#define THEME_COLOR_PROGRESS_GRADIENT_END     lv_palette_lighten(LV_PALETTE_GREY, 20)

/// Shadow width behind a launcher/pinned-link icon button (ui_main.cpp's
/// create_app_icon(), ui_pinned_links.cpp -- both draw the same icon shape).
#define THEME_ICON_BUTTON_SHADOW_WIDTH   30

// ---------------------------------------------------------------------------
// Microphone level meter (ui_microphone.cpp)
// ---------------------------------------------------------------------------

#define MIC_METER_BAR_WIDTH          8
#define MIC_METER_BAR_HEIGHT         75
#define MIC_METER_BAR_SPACING        2
#define MIC_METER_CHANNEL_Y_OFFSET   90   ///< vertical gap between the left and right rows

/// Each bar's color sweeps across a hue range by level; left/right channels
/// use disjoint ranges so the two rows read as distinct at a glance.
#define MIC_METER_HUE_LEFT_START      180
#define MIC_METER_HUE_LEFT_END        360
#define MIC_METER_HUE_RIGHT_START     0
#define MIC_METER_HUE_RIGHT_END       180
#define MIC_METER_SATURATION          100
#define MIC_METER_VALUE                100

// ---------------------------------------------------------------------------
// Walkie-talkie push-to-talk visuals (ui_walkie.cpp)
// ---------------------------------------------------------------------------

#define WALKIE_MIC_BUTTON_DIAMETER   72   ///< centre mic circle diameter
#define WALKIE_RIPPLE_GROWTH_PX      64   ///< how far the talking-indicator rings expand

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

/// Digital clock hour/minute box fill opacity. Was LV_OPA_20 (barely visible
/// against the near-black screen, part of the "clock is smushed/too dark"
/// bugfix) -- bumped for contrast. "Smushed together" was actually caused by
/// the clock being squeezed into a 55%-height band shared with battery/pinned
/// widgets on the home screen; see ui_clockface.cpp, which now gives it the
/// full screen the way src/factory's setupClock() always had.
#define CLOCK_DIGITAL_BOX_BG_OPA   LV_OPA_40

// ---------------------------------------------------------------------------
// Boot button (GPIO0) -- short press (go home / dismiss toast) and long press
// (flashlight toggle). See plans/boot-button-input-plan.md.
// ---------------------------------------------------------------------------

/// How long the raw pin reading must stay low before a press is accepted as
/// real (not switch bounce).
#define BOOT_BUTTON_DEBOUNCE_MS     30
/// Held at least this long before release counts as a long press rather than
/// a short press.
#define BOOT_BUTTON_LONG_PRESS_MS   500

// ---------------------------------------------------------------------------
// Power button (PMU side key) -- single click toggles the display, a second
// click within this window instead opens the power menu.
// ---------------------------------------------------------------------------

#define POWER_BUTTON_DOUBLE_PRESS_WINDOW_MS   350

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

/// Battery-saver override: at or below this percent (and not charging), the
/// radio is forced off regardless of the user's own lora_enabled preference
/// (hal_interface.h's hw_get_lora_enabled(), untouched by this), to preserve
/// what battery is left for the watch's core functions. Deliberately below
/// LOW_BATTERY_WARNING_PERCENT (a warning only) and above the hard-shutdown
/// floor in ui_main.cpp's hw_device_poll() (~3300 mV, roughly single-digit
/// percent) -- the radio gives up its share of the battery budget well before
/// the watch shuts down outright. _REARM_PERCENT is the hysteresis gap that
/// clears the override, mirroring LOW_BATTERY_WARNING_REARM_PERCENT's pattern
/// so the radio doesn't flap on/off right at the threshold.
#define LORA_BATTERY_SAVER_PERCENT         10
#define LORA_BATTERY_SAVER_REARM_PERCENT   15

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
