/**
 * @file      ui_home.cpp
 * @license   MIT
 * @brief     Home screen: clock, battery, pinned links.
 *
 * Composes the three home-screen pieces inside menu_panel (tile (0,0)),
 * partitioning its safe-area-visible height top to bottom: clock band,
 * battery band, pinned-links band. Each band is handed to safe_area_place()
 * so nothing renders under the T-Watch-Ultra's curved bezel.
 */
#include "ui_home.h"
#include "ui_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"
#include "ui_clock_digital.h"
#include "ui_clock_analog.h"
#include "ui_battery_status.h"
#include "ui_pinned_links.h"
#include "usable_area/usable_area.h"

// Single source of truth is NVS (user_setting_params_t, hal_interface.h),
// seeded at ui_home_build() and written back on every change. Deliberately not
// RTC_DATA_ATTR: a phone-set clock face must survive a full power cycle.
static uint8_t s_clock_mode = CLOCK_MODE_DEFAULT;
static bool s_clock_settings_seeded = false;

static lv_obj_t *s_clock_container = NULL;
static lv_obj_t *s_clock_widget = NULL;

static void destroy_clock_widget(void)
{
    if (!s_clock_widget) {
        return;
    }
    if (s_clock_mode == CLOCK_MODE_ANALOG) {
        ui_clock_analog_destroy(s_clock_widget);
    } else {
        ui_clock_digital_destroy(s_clock_widget);
    }
    s_clock_widget = NULL;
}

static void build_clock_widget(void)
{
    if (s_clock_mode == CLOCK_MODE_ANALOG) {
        s_clock_widget = ui_clock_analog_create(s_clock_container);
    } else {
        s_clock_widget = ui_clock_digital_create(s_clock_container);
    }
}

static void persist_clock_mode(void)
{
    user_setting_params_t settings;
    hw_get_user_setting(settings);
    settings.clock_mode = s_clock_mode;
    hw_set_user_setting(settings);
}

static void home_settings_listener(GbStateChange change)
{
    if (change != GB_CHANGE_SETTINGS) {
        return;
    }
    if (!gb_app.settings().has_clock_mode) {
        return;
    }
    const std::string &mode = gb_app.settings().clock_mode;
    uint8_t new_mode;
    if (mode == "analog") {
        new_mode = CLOCK_MODE_ANALOG;
    } else if (mode == "digital") {
        new_mode = CLOCK_MODE_DIGITAL;
    } else {
        return;
    }
    if (new_mode == s_clock_mode) {
        return;
    }
    destroy_clock_widget();
    s_clock_mode = new_mode;
    persist_clock_mode();
    gb_app.reportClockMode(s_clock_mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
    build_clock_widget();
}

static void clock_tap_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    destroy_clock_widget();
    s_clock_mode = (s_clock_mode == CLOCK_MODE_DIGITAL) ? CLOCK_MODE_ANALOG : CLOCK_MODE_DIGITAL;
    build_clock_widget();
    persist_clock_mode();
    gb_app.reportClockMode(s_clock_mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
}

void ui_home_build(lv_obj_t *parent)
{
    if (!s_clock_settings_seeded) {
        s_clock_settings_seeded = true;
        user_setting_params_t settings;
        hw_get_user_setting(settings);
        s_clock_mode = (settings.clock_mode == CLOCK_MODE_ANALOG) ? CLOCK_MODE_ANALOG
                                                                 : CLOCK_MODE_DIGITAL;
        gb_app.reportClockMode(s_clock_mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
        app_gb_add_listener(home_settings_listener);
    }

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    int32_t screen_h = safe_area_screen_height();

    int32_t clock_h   = (screen_h * 55) / 100;
    int32_t battery_h = (screen_h * 12) / 100;
    int32_t pinned_y  = clock_h + battery_h;
    int32_t pinned_h  = screen_h - pinned_y;

    s_clock_container = safe_area_place(parent, 0, clock_h);
    if (s_clock_container) {
        lv_obj_add_flag(s_clock_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_clock_container, clock_tap_cb, LV_EVENT_CLICKED, NULL);
        build_clock_widget();
    }

    lv_obj_t *battery_area = safe_area_place(parent, clock_h, battery_h);
    if (battery_area) {
        ui_battery_status_create(battery_area);
    }

    lv_obj_t *pinned_area = safe_area_place(parent, pinned_y, pinned_h);
    if (pinned_area) {
        ui_pinned_links_build(pinned_area);
    }
}
