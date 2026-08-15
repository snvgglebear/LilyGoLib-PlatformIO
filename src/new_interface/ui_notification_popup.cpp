/**
 * @file      ui_notification_popup.cpp
 * @license   MIT
 * @brief     Auto-dismissing toast for newly arrived Gadgetbridge notifications.
 *
 * Registers one app_gadgetbridge.h listener. gb_app.notifications() is the
 * full current list, newest first (GbApp::onNotify() inserts at the front --
 * see gadgetbridge_ble/gb_app.cpp), so "a new one arrived" is detected by
 * comparing the front id/list size against what was last seen, across calls.
 * That distinguishes a genuine arrival from a dismiss/dismiss-all, both of
 * which only ever shrink the list.
 *
 * The toast itself is a plain non-modal overlay on lv_layer_top(), not
 * create_msgbox() -- create_msgbox() grabs the input group to make a dialog
 * modal, which would steal focus from whatever app is currently open just to
 * show a few seconds of text.
 */
#include "ui_notification_popup.h"
#include "ui_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"

// Single source of truth is NVS (user_setting_params_t, hal_interface.h),
// seeded at ui_notification_popup_init() and written back on every change.
// Deliberately not RTC_DATA_ATTR: these must survive a full power cycle.
static uint32_t s_timeout_ms = NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS;
static bool s_vibrate = NOTIFICATION_POPUP_DEFAULT_VIBRATE;

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_dismiss_timer = NULL;

static int32_t s_last_id = -1;      ///< id of the most-recently-seen newest notification
static size_t s_last_count = 0;     ///< notification count as of the last call

static void dismiss_toast(void)
{
    if (s_dismiss_timer) {
        lv_timer_del(s_dismiss_timer);
        s_dismiss_timer = NULL;
    }
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = NULL;
    }
}

static void dismiss_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    // lv_timer_del() on the timer that is currently executing is a documented
    // safe pattern in LVGL -- the timer module defers the actual free.
    dismiss_toast();
}

static void toast_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    dismiss_toast();
}

static void show_toast(const GbNotification &note)
{
    dismiss_toast();   // replace any toast still showing

    lv_obj_t *toast = lv_obj_create(lv_layer_top());
    lv_obj_set_width(toast, LV_PCT(85));
    lv_obj_set_height(toast, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(toast, THEME_COLOR_BG_TOAST, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_all(toast, 10, 0);
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(toast, toast_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(toast);
    lv_label_set_text(title, note.title.empty() ? note.src.c_str() : note.title.c_str());
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_ON_DARK, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_width(title, LV_PCT(100));

    string body = note.body.empty() ? note.subject : note.body;
    const size_t max_len = 64;
    if (body.size() > max_len) {
        body = body.substr(0, max_len - 3) + "...";
    }
    if (!body.empty()) {
        lv_obj_t *body_label = lv_label_create(toast);
        lv_label_set_text(body_label, body.c_str());
        lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(body_label, THEME_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_width(body_label, LV_PCT(100));
        lv_obj_align_to(body_label, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    }

    s_toast = toast;
    s_dismiss_timer = lv_timer_create(dismiss_timer_cb, s_timeout_ms, NULL);
    lv_timer_set_repeat_count(s_dismiss_timer, 1);

    if (s_vibrate) {
        gb_platform::vibrate(GB_HAPTIC_TAP);
    }
}

static void on_gb_change(GbStateChange change)
{
    if (change != GB_CHANGE_NOTIFICATIONS) {
        return;
    }

    const std::vector<GbNotification> &list = gb_app.notifications();

    bool grew = list.size() > s_last_count;
    bool new_front = !list.empty() && list.front().id != s_last_id;
    if (grew && new_front) {
        show_toast(list.front());
    }

    s_last_count = list.size();
    s_last_id = list.empty() ? -1 : list.front().id;
}

static void on_gb_settings_change(GbStateChange change)
{
    if (change != GB_CHANGE_SETTINGS) {
        return;
    }

    const GbSettings &settings = gb_app.settings();
    if (settings.has_notif_timeout_ms) {
        ui_notification_popup_set_timeout_ms((uint32_t)settings.notif_timeout_ms);
    }
    if (settings.has_notif_vibrate) {
        ui_notification_popup_set_vibrate(settings.notif_vibrate);
    }
}

static void persist_notification_settings(void)
{
    user_setting_params_t settings;
    hw_get_user_setting(settings);
    settings.notif_timeout_ms = s_timeout_ms;
    settings.notif_vibrate = s_vibrate ? 1 : 0;
    hw_set_user_setting(settings);
}

void ui_notification_popup_init(void)
{
    s_last_count = gb_app.notifications().size();
    s_last_id = s_last_count ? gb_app.notifications().front().id : -1;
    app_gb_add_listener(on_gb_change);
    app_gb_add_listener(on_gb_settings_change);

    user_setting_params_t settings;
    hw_get_user_setting(settings);
    s_timeout_ms = settings.notif_timeout_ms ? settings.notif_timeout_ms
                                             : NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS;
    s_vibrate = settings.notif_vibrate != 0;
    gb_app.reportNotificationSettings((int32_t)s_timeout_ms, s_vibrate);
}

void ui_notification_popup_set_timeout_ms(uint32_t ms)
{
    if (ms < NOTIFICATION_POPUP_MIN_TIMEOUT_MS) {
        ms = NOTIFICATION_POPUP_MIN_TIMEOUT_MS;
    } else if (ms > NOTIFICATION_POPUP_MAX_TIMEOUT_MS) {
        ms = NOTIFICATION_POPUP_MAX_TIMEOUT_MS;
    }
    if (ms == s_timeout_ms) {
        return;
    }
    s_timeout_ms = ms;
    persist_notification_settings();
    gb_app.reportNotificationSettings((int32_t)s_timeout_ms, s_vibrate);
}

void ui_notification_popup_set_vibrate(bool enable)
{
    if (enable == s_vibrate) {
        return;
    }
    s_vibrate = enable;
    persist_notification_settings();
    gb_app.reportNotificationSettings((int32_t)s_timeout_ms, s_vibrate);
}

uint32_t ui_notification_popup_get_timeout_ms(void)
{
    return s_timeout_ms;
}

bool ui_notification_popup_get_vibrate(void)
{
    return s_vibrate;
}

bool ui_notification_popup_is_showing(void)
{
    return s_toast != NULL;
}

void ui_notification_popup_dismiss(void)
{
    dismiss_toast();
}
