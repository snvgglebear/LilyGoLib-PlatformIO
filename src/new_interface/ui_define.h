/**
 * @file      ui_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * @brief     Shared UI contract for the factory demo.
 *
 * Every `ui_*.cpp` file includes this header. It defines:
 *   - the `app_t` plug-in interface each launchable app implements,
 *   - the LVGL message IDs used to push data from background work into the UI,
 *   - the `create_*` / `ui_create_*` widget factory helpers implemented in
 *     ui_tools.cpp and ui_theme.cpp, which give every screen a consistent look,
 *   - input-device and power-mode hooks owned by ui_main.cpp,
 *   - a LVGL v8 <-> v9 compatibility shim (see bottom of file).
 *
 * @see LVGL widget documentation: https://docs.lvgl.io/master/details/widgets/index.html
 */
#ifdef ARDUINO
#include <Arduino.h>
#include <LilyGoLib.h>
#include <WiFi.h>
#include <esp_mac.h>        // esp_read_mac() -- used to derive per-device IDs
#else
// RTC_DATA_ATTR places a variable in the ESP32's RTC slow memory so it survives
// deep sleep. There is no such memory on the host, so for the emulator build the
// attribute is defined away and such variables become ordinary globals.
#define RTC_DATA_ATTR
#endif
#include <lvgl.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <time.h>
#include <string.h>
#include "hal_interface.h"

using namespace std;

// Default opacity (0-255 in LVGL terms) applied to several themed backgrounds.
#define DEFAULT_OPA          100

/// Signature of an app's lifecycle callbacks. `parent` is the LVGL container the
/// app should build its widgets into (setup) or is about to have destroyed (exit).
typedef void (*app_func_t)(lv_obj_t *parent);

/**
 * The plug-in interface for a launchable app.
 *
 * Each `ui_<feature>.cpp` exports exactly one global `app_t` (e.g. `radio_app`),
 * and ui_main.cpp registers it with create_app() to place an icon on the
 * launcher grid. Opening the icon calls `setup_func_cb`; leaving the app calls
 * `exit_func_cb`, which must release any timer, task, or radio the app claimed --
 * peripherals are shared, so a leak here breaks the *next* app the user opens.
 */
typedef struct {
    app_func_t setup_func_cb;   ///< build the screen; called when the app is opened
    app_func_t exit_func_cb;    ///< tear down and free resources; called when it is closed
    void *user_data;            ///< optional per-app context, opaque to the launcher
} app_t;


/// Selects which of the two row layouts create_text() should build (icon+text
/// on one line vs. stacked). Used by the settings-style menu pages.
enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

// ---------------------------------------------------------------------------
// LVGL "message" (pub/sub) IDs.
//
// LVGL objects may only be touched from the thread running lv_timer_handler().
// Background producers (BLE callbacks, the audio decoder task, the FFT task)
// therefore do not write widgets directly: they publish on one of these IDs with
// lv_msg_send(), and the widget that subscribed with lv_msg_subscribe_obj()
// receives it safely inside the LVGL context.
//
// The numeric values are arbitrary but must stay unique across the whole app;
// they are grouped by hundreds per subsystem to keep that easy.
// @see https://docs.lvgl.io/master/details/main-components/others.html#messaging
// ---------------------------------------------------------------------------
#define MSG_MENU_NAME_CHANGED    100    ///< menu page title changed
#define MSG_LABEL_PARAM_CHANGE_1 200    ///< generic label value update, slot 1
#define MSG_LABEL_PARAM_CHANGE_2 201    ///< generic label value update, slot 2
#define MSG_TITLE_NAME_CHANGE    203    ///< screen title bar text changed
#define MSG_BLE_SEND_DATA_1      204    ///< payload received over BLE, slot 1
#define MSG_BLE_SEND_DATA_2      205    ///< payload received over BLE, slot 2
#define MSG_MUSIC_TIME_ID        300    ///< audio playback position tick
#define MSG_MUSIC_TIME_END_ID    301    ///< audio playback finished
#define MSG_FFT_ID               400    ///< new FFT bin data for the spectrum view

/// The launcher screen, owned by ui_main.cpp. Apps use it as the object to
/// return to when their back button is pressed.
extern lv_obj_t *main_screen;

// ---------------------------------------------------------------------------
// App registry (ui_main.cpp).
//
// setupGui() builds one AppEntry per app that survives its board/runtime
// gate (#if defined(USING_*), hw_has_keyboard(), ...) and hands the table out
// through app_registry(), so ui_pinned_links.cpp and ui_all_apps.cpp can
// list/launch apps without re-deriving which ones exist on this board.
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;           ///< caption; also the icon's focus/click user data
    const lv_img_dsc_t *icon;   ///< icon bitmap, or NULL
    app_t *app;                 ///< the app's lifecycle callbacks
    int pin_id;                 ///< PinnableApp bit (app_config.h) this entry is, or -1
} AppEntry;

/// The full table of apps registered in setupGui(), in registration order.
const AppEntry *app_registry(size_t *count);

/// Open `app` on tile (0,1): focus its input group, run its setup callback
/// with that tile as the parent, then slide it into view. This is the one
/// launch path -- the pinned-links row, "All Apps", and the original icon row
/// all call it instead of duplicating the click behaviour.
void open_app(app_t *app);

/// Build one launcher-style icon button in `parent`, wired to open_app(app)
/// on click. Used both for the original scrolling icon row and for the
/// pinned-links row.
lv_obj_t *create_app_icon(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app);

// ---------------------------------------------------------------------------
// Themed widget factories (implemented in ui_tools.cpp).
//
// Apps are expected to build their screens out of these rather than raw
// lv_*_create() calls, so styling stays consistent across every app and the
// widgets are automatically added to the active input group (needed for encoder
// and keyboard navigation on the T-LoRa-Pager).
// ---------------------------------------------------------------------------

/// Build a settings row that expands into a sub-page. `widget_create` is invoked
/// to populate that page lazily, the first time the row is opened.
lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb);
/// Static "icon + text" row. `icon` may be an LV_SYMBOL_* string or NULL.
lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant);
/// Labelled slider row. `filter` chooses which LVGL event fires `cb`
/// (LV_EVENT_VALUE_CHANGED for live updates, LV_EVENT_RELEASED for on-commit).
lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter);
/// Labelled on/off toggle row; `chk` is the initial state.
lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb);
/// Labelled push-button row.
lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb);
/// Read-only "name: value" row. Returns the value label so the caller can update it.
lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text);
/// Labelled dropdown row. `options` is LVGL's newline-separated option string.
lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb);
/// Modal dialog. `btns` is a NULL-terminated array of button captions.
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data);
/// Close a dialog from create_msgbox() and restore the previous input group.
void destroy_msgbox(lv_obj_t *msgbox);

// --- Input device access (implemented in ui_main.cpp) -----------------------
/// The rotary-encoder input device, or NULL on boards without one.
lv_indev_t *lv_get_encoder_indev();
/// The physical-keyboard input device, or NULL on boards without one.
lv_indev_t *lv_get_keyboard_indev();
void menu_show();                            ///< reveal the launcher's status/menu bar
void menu_hidden();                          ///< hide it (apps that want the full screen)
/// Point the encoder/keyboard at `group`, so navigation keys move focus within it.
/// @see https://docs.lvgl.io/master/details/main-components/indev.html#groups
void set_default_group(lv_group_t *group);

/// Full-width progress bar with a caption, used by long-running operations
/// (firmware checks, file transfers, radio sweeps).
lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title);

/// Install the app-wide LVGL theme (colours, fonts, default styles). ui_theme.cpp.
void theme_init();

/// Detach/reattach all input devices. Used around modal operations that must not
/// be interrupted, and when the screen is blanked.
void disable_input_devices();
void enable_input_devices();

/// Tell the UI that the device has entered/left low-power mode, so periodic
/// refresh timers can be throttled or stopped.
void set_low_power_mode_flag(bool enable);

/// Suppress the physical keyboard only (e.g. while a text area would otherwise
/// swallow the navigation keys).
void disable_keyboard();
void enable_keyboard();

/// Floating action button pinned over the current screen (typically "back").
lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data);
/// Container styled as the standard scrollable settings menu.
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb);
/// Circular icon button; `image` is an LVGL image descriptor (an `lv_img_dsc_t`
/// from one of the generated files in src/factory/src/).
lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data);

// ---------------------------------------------------------------------------
// LVGL v8 / v9 compatibility shim.
//
// The app is written against LVGL v8 spelling. LVGL v9 renamed a number of
// symbols ("btn" -> "button", lv_mem_* -> lv_malloc/lv_free, the colour-format
// enums, and the precise-coordinate point type). Rather than fork the UI code,
// the v8 names are aliased onto their v9 equivalents here; when building against
// v8 the two accessors that only exist as functions in v9 are supplied instead.
//
// This is what lets the same ui_*.cpp compile against whichever LVGL version
// lib_deps resolves for the selected environment.
// @see LVGL v9 migration guide: https://docs.lvgl.io/master/details/integration/adding-lvgl-to-your-project/index.html
// ---------------------------------------------------------------------------
#if LVGL_VERSION_MAJOR == 9
#define LV_MENU_ROOT_BACK_BTN_ENABLED   LV_MENU_ROOT_BACK_BUTTON_ENABLED
#define lv_menu_back_btn_is_root        lv_menu_back_button_is_root
#define lv_menu_set_mode_root_back_btn  lv_menu_set_mode_root_back_button
#define lv_mem_alloc                    lv_malloc
#define lv_mem_free                     lv_free
#define LV_IMG_CF_ALPHA_8BIT            LV_COLOR_FORMAT_L8
#define lv_point_t                      lv_point_precise_t
#else
// In v8 these are plain struct field reads; v9 promoted them to accessors.
#define lv_timer_get_user_data(x)       (x->user_data)
#define lv_indev_get_type(x)            (x->driver->type)
#endif

#if LVGL_VERSION_MAJOR == 8
// (No v8-only aliases are needed at present; kept as the hook for future ones.)
#endif

// The compass/needle drawing code needs pi. <math.h> only guarantees M_PI when
// the platform exposes the POSIX/BSD extensions, which the ESP32 toolchain does
// not do under strict ISO C++, so define it if it is missing.
#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif
