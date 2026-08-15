/**
 * @file      ui_battery_status.cpp
 * @license   MIT
 * @brief     Battery percent + icon widget for the home screen, plus the
 *            dismissable low-battery warning.
 *
 * Two independent things:
 *   - a small always-visible readout (icon + "NN%"), refreshed on its own
 *     timer, reusing the img_battery/img_batter_low bitmaps already shipped
 *     for the factory clock face;
 *   - a one-shot warning popup that fires once when the charge drops to
 *     LOW_BATTERY_WARNING_PERCENT, shown on lv_layer_top() so it is visible
 *     over whatever app happens to be open, and does not fire again until
 *     the charge has recovered past LOW_BATTERY_WARNING_REARM_PERCENT. This
 *     is purely advisory -- ui_main.cpp's hw_device_poll() still shuts the
 *     device down on its own, much lower, hard threshold regardless of
 *     whether this warning was ever seen.
 */
#include "ui_battery_status.h"
#include "ui_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"

LV_IMG_DECLARE(img_battery);
LV_IMG_DECLARE(img_batter_low);

// Low-battery warning threshold, in percent. Single source of truth is NVS
// (user_setting_params_t, hal_interface.h), seeded at ui_battery_status_create()
// and written back on every change; the app_config.h constant is just the
// boot-time default.
static uint8_t s_low_batt_pct = LOW_BATTERY_WARNING_PERCENT;
static bool s_settings_seeded = false;

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *label;
    lv_timer_t *timer;
    bool warned;     ///< true from the moment the warning fires until re-armed
} battery_status_ctx_t;

/// The one live low-battery warning, or NULL. Independent of ui_msg.cpp's own
/// single-popup slot (lv_scr_act()-parented, used for WiFi/NFC messages) --
/// this one is deliberately raised on lv_layer_top() instead, since it must
/// stay visible even while an app fills tile (0,1).
static lv_obj_t *s_warning_box = NULL;

static void warning_closed_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    destroy_msgbox(s_warning_box);
    s_warning_box = NULL;
}

static void battery_settings_listener(GbStateChange change)
{
    if (change != GB_CHANGE_SETTINGS) {
        return;
    }
    if (!gb_app.settings().has_low_batt_pct) {
        return;
    }
    int32_t pct = gb_app.settings().low_batt_pct;
    if (pct < 5) {
        pct = 5;
    } else if (pct > 50) {
        pct = 50;
    }
    if ((uint8_t)pct == s_low_batt_pct) {
        return;
    }
    s_low_batt_pct = (uint8_t)pct;
    user_setting_params_t settings;
    hw_get_user_setting(settings);
    settings.low_batt_pct = s_low_batt_pct;
    hw_set_user_setting(settings);
    gb_app.reportLowBatteryPercent(pct);
}

static void poll(lv_timer_t *t)
{
    battery_status_ctx_t *ctx = (battery_status_ctx_t *)lv_timer_get_user_data(t);

    monitor_params_t params;
    hw_get_monitor_params(params);

    bool low = params.battery_percent <= s_low_batt_pct;
    lv_image_set_src(ctx->icon, low ? &img_batter_low : &img_battery);
    lv_label_set_text_fmt(ctx->label, "%d%%", params.battery_percent);

    if (low) {
        if (!ctx->warned) {
            ctx->warned = true;
            if (!s_warning_box) {
                static const char *btns[] = {"Dismiss", ""};
                char msg[80];
                snprintf(msg, sizeof(msg), "Battery is low (%d%%). Please charge soon.", params.battery_percent);
                s_warning_box = create_msgbox(lv_layer_top(), "Low Battery", msg, btns, warning_closed_cb, NULL);
            }
        }
    } else if (params.battery_percent >= LOW_BATTERY_WARNING_REARM_PERCENT) {
        ctx->warned = false;
    }
}

lv_obj_t *ui_battery_status_create(lv_obj_t *parent)
{
    if (!s_settings_seeded) {
        s_settings_seeded = true;
        user_setting_params_t settings;
        hw_get_user_setting(settings);
        s_low_batt_pct = settings.low_batt_pct;
        gb_app.reportLowBatteryPercent((int32_t)s_low_batt_pct);
        app_gb_add_listener(battery_settings_listener);
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    battery_status_ctx_t *ctx = (battery_status_ctx_t *)lv_malloc(sizeof(battery_status_ctx_t));
    memset(ctx, 0, sizeof(*ctx));

    lv_obj_t *icon = lv_image_create(cont);
    lv_image_set_src(icon, &img_battery);
    ctx->icon = icon;

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, "--%");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_pad_left(label, 6, 0);
    ctx->label = label;

    ctx->timer = lv_timer_create(poll, BATTERY_POLL_INTERVAL_MS, ctx);
    lv_timer_ready(ctx->timer);   // paint a real reading immediately

    lv_obj_set_user_data(cont, ctx);
    return cont;
}
