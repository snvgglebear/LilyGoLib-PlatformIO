/**
 * @file      ui_pinned_links.cpp
 * @license   MIT
 * @brief     Home screen row of pinned app shortcuts.
 *
 * Reads ui_main.cpp's app_registry() table -- built once in setupGui() with
 * the same board/runtime gates as the original launcher row -- and shows an
 * icon for each entry whose PinnableApp bit (app_config.h) is set in the
 * persisted mask, in registry order, capped at PINNED_APPS_MAX_VISIBLE. A
 * pinned bit with no matching registry entry (PIN_ALARMS today, until a
 * later pass adds an Alarms app) is silently skipped -- there is simply
 * nothing in the table with that pin_id to iterate, so no special-case code
 * is needed for it.
 */
#include "ui_pinned_links.h"
#include "ui_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"

// Single source of truth is NVS (user_setting_params_t, hal_interface.h),
// seeded at first build and written back on every change. Deliberately not
// RTC_DATA_ATTR: a phone-set pinned mask must survive a full power cycle.
static uint32_t pinned_apps_mask = PINNED_APPS_DEFAULT_MASK;

static lv_obj_t *s_row = NULL;
static lv_obj_t *s_parent = NULL;
static bool s_initialized = false;

static void all_apps_click_cb(lv_event_t *e)
{
    app_t *app = (app_t *)lv_event_get_user_data(e);
    open_app(app);
}

static void add_all_apps_icon(lv_obj_t *parent)
{
    extern app_t ui_all_apps_main;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 150, LV_PCT(100));
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_shadow_width(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_LIST "\nAll Apps");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, all_apps_click_cb, LV_EVENT_CLICKED, &ui_all_apps_main);
}

static void pinned_links_settings_listener(GbStateChange change)
{
    if (change != GB_CHANGE_SETTINGS) {
        return;
    }
    if (!gb_app.settings().has_pinned_mask) {
        return;
    }
    uint32_t mask = gb_app.settings().pinned_mask & ((1u << PIN_APP_COUNT) - 1);
    if (mask == pinned_apps_mask) {
        return;
    }
    pinned_apps_mask = mask;
    user_setting_params_t settings;
    hw_get_user_setting(settings);
    settings.pinned_mask = mask;
    hw_set_user_setting(settings);
    gb_app.reportPinnedMask(mask);
    if (s_parent) {
        ui_pinned_links_build(s_parent);
    }
}

void ui_pinned_links_build(lv_obj_t *parent)
{
    if (!s_initialized) {
        s_initialized = true;
        user_setting_params_t settings;
        hw_get_user_setting(settings);
        pinned_apps_mask = settings.pinned_mask & ((1u << PIN_APP_COUNT) - 1);
        gb_app.reportPinnedMask(pinned_apps_mask);
        app_gb_add_listener(pinned_links_settings_listener);
    }
    s_parent = parent;

    if (s_row) {
        lv_obj_del(s_row);
        s_row = NULL;
    }

    // A flex row that scrolls horizontally and snaps each icon to the
    // centre -- the same shape as the original launcher's icon row.
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(row, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    size_t count = 0;
    const AppEntry *apps = app_registry(&count);

    uint32_t shown = 0;
    for (size_t i = 0; i < count && shown < PINNED_APPS_MAX_VISIBLE; i++) {
        if (apps[i].pin_id < 0) {
            continue;
        }
        if (!(pinned_apps_mask & (1u << apps[i].pin_id))) {
            continue;
        }
        create_app_icon(row, apps[i].name, apps[i].icon, apps[i].app);
        shown++;
    }

    add_all_apps_icon(row);

    s_row = row;
    lv_obj_update_snap(row, LV_ANIM_OFF);
}
