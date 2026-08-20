/**
 * @file      settings_screen.cpp
 * @license   MIT
 * @brief     The settings menu. See settings_screen.h.
 *
 * Shape borrowed from src/factory/ui_sys.cpp: an lv_menu whose main page is a
 * list of subpage links, with the root back button doing double duty -- up one
 * level on a subpage, out of the screen on the main page.
 *
 * The one structural departure is where the settings get written. Factory
 * commits in its back-button handler, so any other way out of the page loses
 * the edit. Here every control applies its effect immediately and the single
 * NVS write hangs off LV_EVENT_SCREEN_UNLOAD_START, which LVGL sends to the
 * outgoing screen from both lv_screen_load() and lv_screen_load_anim() -- so it
 * also covers the exit the user does not initiate, where the display sleeps and
 * screen_state's wake callback loads the watch face out from under this page.
 */
#include "settings_screen.h"

#include "app_settings.h"
#include "settings_widgets.h"

#include "../app_config.h"
#include "../gadgetbridge_ble/gb_app.h"
#include "../gadgetbridge_ble/gb_link.h"
#include "../gadgetbridge_ble/gb_platform.h"
#include "../quick_settings_tray/quick_settings_tray.h"
#include "../quick_settings_tray/quick_settings_tray_hal.h"
#include "../screen_state/screen_state.h"
#include "../watch_faces/face_registry.h"

#include <usable_area.h>

#include <stdio.h>

#ifdef ARDUINO
#include <Arduino.h>   // ESP.getFreeHeap(), for the System Info page
#endif

namespace
{

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_menu = nullptr;
lv_obj_t *s_return_to = nullptr;   ///< screen that was active when we opened

/// Live rows on the System Info page, refreshed by s_info_timer.
lv_obj_t *s_info_battery = nullptr;
lv_obj_t *s_info_link = nullptr;
lv_obj_t *s_info_heap = nullptr;
lv_timer_t *s_info_timer = nullptr;

void buildMenu();

// -- System Info -----------------------------------------------------------

void refreshInfo(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    char buf[48];

    if (s_info_battery) {
        const int percent = gb_platform::batteryPercent();
        snprintf(buf, sizeof(buf), "%d%%  %.2fV%s", percent, gb_platform::batteryVolts(),
                 gb_platform::charging() ? "  chg" : "");
        lv_label_set_text(s_info_battery, buf);
    }
    if (s_info_link) {
        lv_label_set_text(s_info_link, gb_link_connected() ? "connected" : "advertising");
    }
    if (s_info_heap) {
#ifdef ARDUINO
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
        lv_label_set_text(s_info_heap, buf);
#else
        lv_label_set_text(s_info_heap, "n/a");
#endif
    }
}

/// Deleting the timer is not optional housekeeping: the menu is rebuilt on
/// every visit, so a timer left running would both accumulate one per visit and
/// write to labels freed with the old menu.
void stopInfoTimer()
{
    if (s_info_timer) {
        lv_timer_delete(s_info_timer);
        s_info_timer = nullptr;
    }
    s_info_battery = s_info_link = s_info_heap = nullptr;
}

// -- control callbacks -----------------------------------------------------

void brightnessChanged(lv_event_t *e)
{
    app_settings_set_brightness(lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)));
}

void timeoutChanged(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    const uint16_t seconds = (uint16_t)lv_slider_get_value(slider);
    app_settings_set_screen_timeout_s(seconds);

    // The value label is the row's own label, so the row reads
    // "Screen timeout   30 s" rather than needing a separate readout.
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    if (seconds == 0) {
        lv_label_set_text(label, "Screen timeout: never");
    } else {
        lv_label_set_text_fmt(label, "Screen timeout: %u s", seconds);
    }
}

void popupDurationChanged(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    const uint16_t ms = (uint16_t)lv_slider_get_value(slider);
    app_settings_set_notif_popup_ms(ms);
    gb_app.reportSettingsChanged();   // §6.8: phone's settings screen should track this too

    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "Popup duration: %u.%u s", ms / 1000u, (ms % 1000u) / 100u);
}

bool switchIsOn(lv_event_t *e)
{
    return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
}

void wristWakeChanged(lv_event_t *e)
{
    app_settings_set_wrist_wake(switchIsOn(e));
}

void analogFaceChanged(lv_event_t *e)
{
    app_settings_set_watch_face(switchIsOn(e) ? WATCH_FACE_ANALOG : WATCH_FACE_DIGITAL);
    gb_app.reportSettingsChanged();   // §6.8: `clock_mode` echo
}

void vibrateMessagesChanged(lv_event_t *e)
{
    app_settings_set_vibrate_messages(switchIsOn(e));
    gb_app.reportSettingsChanged();   // §6.8: `notif_vibrate` echo
}

void vibrateAlertsChanged(lv_event_t *e)
{
    app_settings_set_vibrate_alerts(switchIsOn(e));
}

// -- restore defaults ------------------------------------------------------

void restoreConfirmed(lv_event_t *e)
{
    lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent((lv_obj_t *)lv_event_get_current_target(e))));
    app_settings_restore_defaults();
    gb_app.reportSettingsChanged();   // §6.8: defaults touch all three echoed fields
    buildMenu();    // the old rows are showing the old values
}

void restoreClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_obj_set_width(box, usable_area_screen_width() - 2 * SAFE_INSET);
    lv_msgbox_add_title(box, "Restore defaults?");
    lv_msgbox_add_text(box, "Brightness, timeout, watch face and notification "
                            "settings all go back to their defaults.");
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(box, "Restore"), restoreConfirmed,
                        LV_EVENT_CLICKED, NULL);
    lv_msgbox_add_close_button(box);
}

// -- pages -----------------------------------------------------------------

void buildDisplayPage(lv_obj_t *page)
{
    const AppSettings &s = app_settings();

    settings_slider(page, LV_SYMBOL_IMAGE, "Brightness",
                    qst_hal_brightness_min(), qst_hal_brightness_max(), s.brightness,
                    brightnessChanged, LV_EVENT_VALUE_CHANGED, NULL);

    // The row's label doubles as the readout, so it is created first and handed
    // to the callback as user_data.
    lv_obj_t *timeout_row = settings_row(page, LV_SYMBOL_EYE_CLOSE, NULL, true);
    lv_obj_t *timeout_label = lv_label_create(timeout_row);
    lv_obj_set_flex_grow(timeout_label, 1);
    if (s.screen_timeout_s == 0) {
        lv_label_set_text(timeout_label, "Screen timeout: never");
    } else {
        lv_label_set_text_fmt(timeout_label, "Screen timeout: %u s", s.screen_timeout_s);
    }
    lv_obj_t *timeout_slider = lv_slider_create(timeout_row);
    lv_obj_set_flex_grow(timeout_slider, 1);
    lv_obj_add_flag(timeout_slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_slider_set_range(timeout_slider, APP_SCREEN_TIMEOUT_MIN_S, APP_SCREEN_TIMEOUT_MAX_S);
    lv_slider_set_value(timeout_slider, s.screen_timeout_s, LV_ANIM_OFF);
    lv_obj_add_event_cb(timeout_slider, timeoutChanged, LV_EVENT_VALUE_CHANGED, timeout_label);

#ifdef HAS_WRIST_TILT_SENSOR
    settings_switch(page, LV_SYMBOL_REFRESH, "Wrist-raise wake", s.wrist_wake != 0,
                    wristWakeChanged, NULL);
#else
    // Shown rather than hidden, so "why is this missing" is answered on the
    // page instead of only in the source -- src/new_interface/ui_sys.cpp does
    // the same for its LoRa rows.
    settings_value(page, LV_SYMBOL_REFRESH, "Wrist-raise wake", "no sensor");
#endif
}

void buildWatchFacePage(lv_obj_t *page)
{
    settings_switch(page, LV_SYMBOL_SETTINGS, "Analog face",
                    watch_face_current() == WATCH_FACE_ANALOG, analogFaceChanged, NULL);
    settings_value(page, NULL, "Off", watch_face_name(WATCH_FACE_DIGITAL));
    settings_value(page, NULL, "On", watch_face_name(WATCH_FACE_ANALOG));
}

void buildNotificationsPage(lv_obj_t *page)
{
    const AppSettings &s = app_settings();

    lv_obj_t *popup_row = settings_row(page, LV_SYMBOL_ENVELOPE, NULL, true);
    lv_obj_t *popup_label = lv_label_create(popup_row);
    lv_obj_set_flex_grow(popup_label, 1);
    lv_label_set_text_fmt(popup_label, "Popup duration: %u.%u s",
                          s.notif_popup_ms / 1000u, (s.notif_popup_ms % 1000u) / 100u);
    lv_obj_t *popup_slider = lv_slider_create(popup_row);
    lv_obj_set_flex_grow(popup_slider, 1);
    lv_obj_add_flag(popup_slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_slider_set_range(popup_slider, APP_NOTIF_POPUP_MIN_MS, APP_NOTIF_POPUP_MAX_MS);
    lv_slider_set_value(popup_slider, s.notif_popup_ms, LV_ANIM_OFF);
    lv_obj_add_event_cb(popup_slider, popupDurationChanged, LV_EVENT_VALUE_CHANGED, popup_label);

    settings_switch(page, LV_SYMBOL_BELL, "Vibrate: messages", s.vibrate_messages != 0,
                    vibrateMessagesChanged, NULL);
    settings_switch(page, LV_SYMBOL_CALL, "Vibrate: calls/alarms", s.vibrate_alerts != 0,
                    vibrateAlertsChanged, NULL);
}

void buildInfoPage(lv_obj_t *page)
{
    settings_value(page, NULL, "Firmware", GB_FW_VERSION);
    settings_value(page, NULL, "Board", gb_platform::hardwareName());
    settings_value(page, NULL, "BLE name", gb_link_device_name());
    s_info_link    = settings_value(page, NULL, "Link", "--");
    s_info_battery = settings_value(page, NULL, "Battery", "--");
    settings_value(page, NULL, "LVGL", lv_version_info());
    settings_value(page, NULL, "Built", __DATE__ " " __TIME__);
    s_info_heap    = settings_value(page, NULL, "Free heap", "--");

    s_info_timer = lv_timer_create(refreshInfo, 1000, NULL);
    lv_timer_ready(s_info_timer);   // fill the live rows now, not in a second
}

// -- menu ------------------------------------------------------------------

/// Add a subpage and the main-page row that opens it, in one call -- the
/// lv_menu two-step (create the page, then point a row at it) otherwise
/// repeats five times.
lv_obj_t *addSubpage(lv_obj_t *main_page, const char *symbol, const char *text)
{
    lv_obj_t *page = lv_menu_page_create(s_menu, (char *)text);
    lv_obj_t *row = settings_row(main_page, symbol, text, /*stacked*/ false);
    lv_menu_set_load_page_event(s_menu, row, page);
    return page;
}

/// LV_EVENT_CLICKED on the menu: LVGL routes the root back button's click here.
/// On a subpage the widget has already gone up one level by the time this runs,
/// so the only thing left to decide is whether a click on the *main* page means
/// "leave the settings screen".
void menuClicked(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (!lv_menu_back_button_is_root(s_menu, obj)) {
        return;
    }
    if (lv_menu_get_cur_main_page(s_menu) != nullptr &&
        lv_menu_get_cur_sidebar_page(s_menu) == nullptr) {
        // Already at the top: this back button means "out".
        lv_screen_load(s_return_to ? s_return_to : lv_screen_active());
    }
}

void buildMenu()
{
    stopInfoTimer();
    lv_obj_clean(s_screen);

    // lv_menu's header (and its back button) sits top-left, the worst place on
    // the Ultra's curve, so the whole menu goes inside the safe rect rather
    // than filling the screen.
    lv_obj_t *area = usable_area_rect(s_screen);

    s_menu = lv_menu_create(area);
    lv_obj_set_size(s_menu, LV_PCT(100), LV_PCT(100));
    lv_menu_set_mode_root_back_button(s_menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
    lv_obj_add_event_cb(s_menu, menuClicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *main_page = lv_menu_page_create(s_menu, (char *)"Settings");

    buildDisplayPage(addSubpage(main_page, LV_SYMBOL_IMAGE, "Display & Backlight"));
    buildWatchFacePage(addSubpage(main_page, LV_SYMBOL_SETTINGS, "Watch Face"));
    buildNotificationsPage(addSubpage(main_page, LV_SYMBOL_BELL, "Notifications"));
    buildInfoPage(addSubpage(main_page, LV_SYMBOL_LIST, "System Info"));

    settings_button(main_page, LV_SYMBOL_WARNING, "Restore defaults", "Reset",
                    restoreClicked, NULL);

    lv_menu_set_page(s_menu, main_page);
}

// -- screen ----------------------------------------------------------------

/// Swipe down opens the quick-settings tray, matching every other
/// non-watchface screen. Swipe up is deliberately unhandled: leaving is the
/// back button's job, and a stray upward swipe should not discard the page.
void onSettingsGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_scroll_obj(indev) != NULL) {
        return;   // the menu's own page is scrollable -- see app_setup.cpp
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        quick_settings_tray_open();
    }
}

/// One NVS write per visit, on the way out -- whichever way out that is.
void onScreenUnload(lv_event_t *e)
{
    LV_UNUSED(e);
    stopInfoTimer();
    app_settings_flush();
}

} // namespace

void settings_screen_init(void)
{
    s_screen = lv_obj_create(NULL);
    usable_area_style_screen(s_screen);   // usable_area_init() only styled the boot screen
    lv_obj_add_event_cb(s_screen, onSettingsGesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_screen, onScreenUnload, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

void settings_screen_open(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_t *active = lv_screen_active();
    if (active != s_screen) {
        s_return_to = active;
    }
    buildMenu();
    lv_screen_load(s_screen);
}
