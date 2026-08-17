/**
 * @file      ui_clockface.cpp
 * @license   MIT
 * @brief     Clockface screen: full-screen digital or analog clock, tap to
 *            toggle between them.
 *
 * This used to be the top band of the home screen (ui_home.cpp), squeezed
 * into 55% of the screen height alongside battery status and pinned links --
 * which was the actual cause of the "digital clock is smushed together"
 * bugfix (src/custom_interface/plan.md's interface_bugfixes): the digital
 * clock's box/font percentages (ui_clock_digital.cpp) were tuned against a
 * full-screen container (matching src/factory's setupClock(), which draws
 * straight on lv_screen_active()), so shrinking that container to a 55%
 * band cramped everything inside it. Per that same bugfix list, the
 * clockface is now its own tile (ui_main.cpp's clock_panel), reached by
 * swiping to/from the home screen or the boot button's short press
 * (plans/boot-button-input-plan.md) -- giving ui_clock_digital_create() the
 * full safe area back, unchanged, is what actually fixes the spacing.
 */
#include "ui_clockface.h"
#include "ui_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"
#include "ui_clock_digital.h"
#include "ui_clock_analog.h"
#include <usable_area.h>

// Single source of truth is NVS (user_setting_params_t, hal_interface.h),
// seeded at ui_clockface_build() and written back on every change.
// Deliberately not RTC_DATA_ATTR: a phone-set clock face must survive a full
// power cycle.
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

static void clockface_settings_listener(GbStateChange change)
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

void ui_clockface_build(lv_obj_t *parent)
{
    if (!s_clock_settings_seeded) {
        s_clock_settings_seeded = true;
        user_setting_params_t settings;
        hw_get_user_setting(settings);
        s_clock_mode = (settings.clock_mode == CLOCK_MODE_ANALOG) ? CLOCK_MODE_ANALOG
                                                                 : CLOCK_MODE_DIGITAL;
        gb_app.reportClockMode(s_clock_mode == CLOCK_MODE_ANALOG ? "analog" : "digital");
        app_gb_add_listener(clockface_settings_listener);
    }

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    int32_t screen_h = usable_area_screen_height();

    s_clock_container = usable_area_place(parent, 0, screen_h);
    if (s_clock_container) {
        lv_obj_add_flag(s_clock_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_clock_container, clock_tap_cb, LV_EVENT_CLICKED, NULL);
        build_clock_widget();
    }
}
