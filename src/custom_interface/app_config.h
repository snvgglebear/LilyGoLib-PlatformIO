#pragma once

/**
 * @file      app_config.h
 * @license   MIT
 * @brief     Every user-adjustable default, range, font and item size in
 *            custom_interface, in one place.
 *
 * Two halves:
 *
 *   BEHAVIOUR   what the watch *does* by default and how far the settings
 *               page will let it be pushed. The settings page
 *               (settings/settings_screen.cpp) reads its slider bounds from
 *               here and the settings store (settings/app_settings.cpp) reads
 *               its defaults from here, so "what does this reset to" and "how
 *               far can it go" are answered by one file rather than by two
 *               that have to agree.
 *
 *   APPEARANCE  what it *looks* like: every font choice and every widget
 *               dimension the app sets explicitly rather than inheriting from
 *               the LVGL theme. Resize the UI here instead of hunting through
 *               gb_ui.cpp, quick_settings_tray.cpp and the watch faces.
 *
 * This app targets the T-Watch-Ultra's 410x502 panel and nothing else, so
 * every size below is a single number rather than a per-board pair, and no
 * module branches on the display resolution.
 *
 * Colors, angles, timings and behaviour flags that belong to exactly one
 * module still live with that module (batman_dial.h keeps the dial's palette
 * and its 360-degree tick math, for instance) -- this header is the sizes and
 * the knobs, not every constant in the app.
 *
 * The APP_FONT_* entries are macros, not constants, so this header stays free
 * of an <lvgl.h> dependency: each one expands to an `&lv_font_*` address and
 * is only ever substituted into a .cpp that already includes LVGL.
 */

#include <stddef.h>
#include <stdint.h>

// ===========================================================================
// BEHAVIOUR
// ===========================================================================

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
// Reserved -- phone-synced pass-through, no watch consumer yet
// ---------------------------------------------------------------------------
/// These three fields are persisted on the watch and echoed to Gadgetbridge
/// (via the §5.14/§6.8 settings message) purely so the phone's preferences
/// stop writing into the void: Gadgetbridge already offers UI for pinned_mask
/// and low_batt_pct, and the parent settings-sync plan reserves lora_enabled.
/// None of them has a subsystem on the watch that reads the value today; see
/// plans/settings-sync-delta-plan.md §2.
constexpr uint32_t APP_PINNED_MASK_DEFAULT   = 0;   ///< no launcher tiles pinned
constexpr uint8_t  APP_LOW_BATT_DEFAULT_PCT  = 20;
constexpr uint8_t  APP_LOW_BATT_MIN_PCT      = 5;   ///< matches Gadgetbridge's own clamp
constexpr uint8_t  APP_LOW_BATT_MAX_PCT      = 50;
constexpr bool     APP_LORA_ENABLED_DEFAULT  = false;   ///< fail-closed

// ===========================================================================
// APPEARANCE
// ===========================================================================

// ---------------------------------------------------------------------------
// Fonts -- shared roles
// ---------------------------------------------------------------------------
/**
 * Three sizes used across the Gadgetbridge screens and the settings page.
 * LVGL ships montserrat in even sizes only and 48 is the largest; a size has
 * to be enabled in lv_conf.h (hardware) or the LV_FONT_MONTSERRAT_* build
 * flags (emulator) before it can be named here.
 */
#define APP_FONT_HUGE      &lv_font_montserrat_48   ///< clock, grid tile icons
/// Body text: list rows, button labels, settings rows, the music track title.
#define APP_FONT_BODY      &lv_font_montserrat_18
/// Captions: status bar, hints, timestamps, chat bubbles.
#define APP_FONT_CAPTION   &lv_font_montserrat_16

// ---------------------------------------------------------------------------
// Fonts -- simple watch face
// ---------------------------------------------------------------------------
/// 48 px is the largest font LVGL ships, so the clock cannot get bigger this
/// way -- the face scales the *label* instead (APP_FACE_TIME_SCALE).
#define APP_FONT_FACE_TIME   &lv_font_montserrat_48
#define APP_FONT_FACE_DATE   &lv_font_montserrat_20
#define APP_FONT_FACE_BATT   &lv_font_montserrat_16

// ---------------------------------------------------------------------------
// Fonts -- quick settings tray
// ---------------------------------------------------------------------------
/// The tray is a fixed-height overlay rather than a flex page, so its fonts
/// are single values: growing them without also growing
/// APP_QST_HEADER_HEIGHT will clip the header.
#define APP_FONT_QST_TIME    &lv_font_montserrat_28   ///< the big clock
#define APP_FONT_QST_DATE    &lv_font_montserrat_14
#define APP_FONT_QST_ICON    &lv_font_montserrat_20   ///< battery/brightness/grabber/gear
#define APP_FONT_QST_VALUE   &lv_font_montserrat_14   ///< the "83%" readouts

// ---------------------------------------------------------------------------
// Fonts -- analog dial face
// ---------------------------------------------------------------------------
/// Hour numerals, and the battery percentage on the arc. The dial derives its
/// numeral radius and readout offset from lv_font_get_line_height() of these,
/// so changing either re-places itself without a second constant to match.
#define APP_FONT_DIAL_NUMERAL   &lv_font_montserrat_28
#define APP_FONT_DIAL_READOUT   &lv_font_montserrat_20

// ---------------------------------------------------------------------------
// Settings page
// ---------------------------------------------------------------------------
/// Height of one settings row. Well above a fingertip, because these rows are
/// scrolled past as often as they are aimed at.
constexpr int32_t APP_SETTINGS_ROW_HEIGHT = 56;

/// Breathing room above a section heading, separating it from the last row of
/// the section before it.
constexpr int32_t APP_SETTINGS_SECTION_PAD_TOP = 10;

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- tap targets
// ---------------------------------------------------------------------------
/**
 * LVGL's theme sizes buttons for a mouse/stylus; this app is touched with a
 * fingertip on a wrist-worn screen, so every button in gb_ui.cpp reads its
 * size from here instead of the theme default.
 */
/// Height of a standalone content button: "Ring my phone", "Dismiss all",
/// thread view back/reply/dismiss, music transport/volume. Width stays
/// content-sized (the label plus padding), floored by APP_GB_BUTTON_MIN_WIDTH.
constexpr int32_t APP_GB_BUTTON_HEIGHT = 50;

/// Minimum width for a content button, so an icon-only button (e.g. the
/// music transport symbols) still gets a reasonably sized tap target.
constexpr int32_t APP_GB_BUTTON_MIN_WIDTH = 64;

/// Height of a msgbox header/footer strip -- close, back, and the action
/// buttons on it are LV_PCT(100) of this, so setting it sizes them too.
constexpr int32_t APP_GB_MSGBOX_STRIP_HEIGHT = 50;

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- status bar
// ---------------------------------------------------------------------------
/// Status bar height. Sized to hold the home button, not just the two labels
/// -- see APP_GB_STATUS_BUTTON_SIZE.
constexpr int32_t APP_GB_STATUS_BAR_HEIGHT = 40;

/// Inset at each end of the strip, so the link and battery labels are not
/// flush against the edge of the safe rect.
constexpr int32_t APP_GB_STATUS_BAR_PAD_HOR = 8;

/// Square side of the status bar's back-to-grid button. Smaller than
/// APP_GB_BUTTON_HEIGHT because it shares a strip with the link/battery
/// labels; APP_GB_STATUS_BUTTON_EXT_CLICK makes the *tappable* area
/// finger-sized.
constexpr int32_t APP_GB_STATUS_BUTTON_SIZE      = 34;
constexpr int32_t APP_GB_STATUS_BUTTON_EXT_CLICK = 10;  ///< invisible tap margin

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- launcher grid
// ---------------------------------------------------------------------------
/// Column and row counts, and the gap/padding around the tiles. Five entries
/// in a 2x3 leaves the bottom-right cell empty; a sixth fills it, and nothing
/// below needs to change to accommodate one.
constexpr int32_t APP_GB_GRID_COLS = 2;
constexpr int32_t APP_GB_GRID_ROWS = 3;
constexpr int32_t APP_GB_GRID_GAP  = 10;
constexpr int32_t APP_GB_GRID_PAD  = 8;

/// Gap between a tile's icon and its name.
constexpr int32_t APP_GB_TILE_GAP = 6;

/// Whether a grid tile tap animates to its page. Off by default: an animated
/// scroll from the grid to Music sweeps *through* Watch/Chats/Alerts, i.e.
/// several full-screen redraws for a jump the user already committed to with
/// a tap. Swipes stay animated regardless -- that animation is lv_tabview's
/// own, driven by the finger.
constexpr bool APP_GB_GRID_ANIMATE_TAB_CHANGE = false;

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- page spacing
// ---------------------------------------------------------------------------
/// Row gap on the centred pages (Watch, Music), where the content is short
/// enough that the spacing is what makes it read as a group.
constexpr int32_t APP_GB_TAB_GAP = 10;

/// Row gap on the list pages (Chats, Alerts) and inside the thread view --
/// tighter, because the list itself takes the remaining height.
constexpr int32_t APP_GB_LIST_GAP = 6;

/// Gap between the thread view's back button and its title.
constexpr int32_t APP_GB_HEADER_GAP = 8;

/// Width of the scrolling track title on the Music page. Short of 100% so the
/// scroll has somewhere to run and the text is not flush to the safe edge.
constexpr int32_t APP_GB_MUSIC_TRACK_WIDTH_PCT = 90;

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- chat bubbles
// ---------------------------------------------------------------------------
/// Padding inside a bubble, and inside the thread view around the whole
/// conversation.
constexpr int32_t APP_GB_BUBBLE_PAD = 8;
constexpr int32_t APP_GB_THREAD_PAD = 8;

/// Corner rounding on a bubble.
constexpr int32_t APP_GB_BUBBLE_RADIUS = 10;

/// How much of the thread body's width one bubble may take, so a long message
/// still reads as a bubble rather than as a full-width paragraph.
constexpr int32_t APP_GB_BUBBLE_MAX_WIDTH_PCT = 80;

// ---------------------------------------------------------------------------
// Gadgetbridge screens -- text truncation
// ---------------------------------------------------------------------------
/// Characters kept before an ellipsis in a one-line preview. In characters
/// rather than pixels because the fonts are proportional and the list row is
/// the thing being fitted, not the glyphs.
constexpr size_t APP_GB_ALERT_PREVIEW_CHARS = 40;
constexpr size_t APP_GB_CHAT_PREVIEW_CHARS  = 44;

// ---------------------------------------------------------------------------
// Quick settings tray
// ---------------------------------------------------------------------------
/// The tray drops from the top of the screen, so its total height is also how
/// far it travels. The three bands below must sum to no more than this.
constexpr int32_t APP_QST_TRAY_HEIGHT       = 230;
constexpr int32_t APP_QST_HEADER_HEIGHT     = 110;  ///< clock + battery
constexpr int32_t APP_QST_BRIGHTNESS_HEIGHT = 70;   ///< icon + slider + readout
constexpr int32_t APP_QST_FOOTER_HEIGHT     = 50;   ///< grabber + gear

/// The battery meter under the charge icon.
constexpr int32_t APP_QST_BATT_BAR_WIDTH  = 80;
constexpr int32_t APP_QST_BATT_BAR_HEIGHT = 12;
constexpr int32_t APP_QST_BATT_COL_GAP    = 4;   ///< icon -> bar -> percentage

/// Brightness slider width, as a percentage of its band. The rest of the band
/// is the icon on the left and the percentage on the right.
constexpr int32_t APP_QST_SLIDER_WIDTH_PCT = 50;

/// The gear is pinned to the right edge of the footer rather than flowed, so
/// it needs its own inset; the ext-click margin then makes a 20 px glyph a
/// fingertip-sized target without drawing anything bigger.
constexpr int32_t APP_QST_GEAR_PAD_RIGHT = 8;
constexpr int32_t APP_QST_GEAR_EXT_CLICK = 12;

// ---------------------------------------------------------------------------
// Simple watch face
// ---------------------------------------------------------------------------
/// Gap between the face's four stacked elements.
constexpr int32_t APP_FACE_PAD_ROW = 8;

/// Draw-time magnification of the clock label, since 48 px is as large as
/// LVGL's bitmap fonts go. Scaling does not grow the box flex reserves, hence
/// APP_FACE_TIME_MARGIN above and below to keep the enlarged render off the
/// date beneath it -- raise them together.
constexpr int32_t APP_FACE_TIME_SCALE  = 2;
constexpr int32_t APP_FACE_TIME_MARGIN = 24;

/// The battery meter along the bottom.
constexpr int32_t APP_FACE_BATT_BAR_WIDTH_PCT = 50;
constexpr int32_t APP_FACE_BATT_BAR_HEIGHT   = 20;

// ---------------------------------------------------------------------------
// Analog dial face
// ---------------------------------------------------------------------------
/// Sizes are px unless the name says otherwise. Colors, tick counts and the
/// arc's angles stay in watch_faces/batman_dial.h.

/// Kept clear inside the panel (or usable area) before anything is drawn.
constexpr int32_t APP_DIAL_FACE_MARGIN = 10;

/// Radial room reserved OUTSIDE the dial for the battery arc and its readout.
/// Widening it shrinks the dial and everything sized off it. Must be at least
/// APP_DIAL_ARC_GAP + APP_DIAL_ARC_WIDTH for the arc to fit its space.
constexpr int32_t APP_DIAL_ARC_SPACING = 20;

/// Tick marks around the rim.
constexpr int32_t APP_DIAL_SMALL_TICK_LENGTH = 6;
constexpr int32_t APP_DIAL_LARGE_TICK_LENGTH = 15;
constexpr int32_t APP_DIAL_SMALL_TICK_WIDTH  = 2;
constexpr int32_t APP_DIAL_LARGE_TICK_WIDTH  = 3;

/// Gap between the inner end of a major tick and the hour numeral below it.
/// A real knob only because the numerals are placed by hand -- lv_scale's own
/// label distance is a #define inside lv_scale.c and is not stylable.
constexpr int32_t APP_DIAL_NUMERAL_GAP = 12;

/// Hand lengths as a percentage of the dial radius, and their stroke widths.
constexpr int32_t APP_DIAL_HOUR_HAND_PCT   = 50;
constexpr int32_t APP_DIAL_MIN_HAND_PCT    = 75;
constexpr int32_t APP_DIAL_SEC_HAND_PCT    = 90;
constexpr int32_t APP_DIAL_HOUR_HAND_WIDTH = 10;
constexpr int32_t APP_DIAL_MIN_HAND_WIDTH  = 4;
constexpr int32_t APP_DIAL_SEC_HAND_WIDTH  = 2;

/// Battery arc stroke, and the gap it leaves on both sides (dial edge -> arc,
/// and arc -> readout).
constexpr int32_t APP_DIAL_ARC_WIDTH = 8;
constexpr int32_t APP_DIAL_ARC_GAP   = 8;

/// Ring drawn around the dial face itself.
constexpr int32_t APP_DIAL_BORDER_WIDTH = 3;

/// Floor on the computed radius, so a very small (or not-yet-laid-out) parent
/// yields a tiny dial rather than a negative one.
constexpr int32_t APP_DIAL_MIN_RADIUS = 10;

/// Square side assumed when the parent still measures 0x0 at build time --
/// should not normally happen, and only has to be visible enough to notice.
constexpr int32_t APP_DIAL_FALLBACK_SIZE = 120;
