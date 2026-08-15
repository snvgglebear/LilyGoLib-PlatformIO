/**
 * @file      ui_all_apps.cpp
 * @license   MIT
 * @brief     Full app listing -- every entry in ui_main.cpp's app_registry(),
 *            reached from the home screen's pinned-links row.
 *
 * A normal app_t like any other ui_<feature>.cpp: a create_menu() page with
 * one create_text()-shaped list row per registered app. Structured after
 * ui_gps.cpp / ui_power.cpp (see ui_define.h's app_t contract) -- floating
 * back button on touch boards, root back button otherwise, both routed
 * through the same back_event_handler back to menu_show().
 */
#include "ui_define.h"
#include "app_config.h"

static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

static void app_row_click_cb(lv_event_t *e)
{
    app_t *app = (app_t *)lv_event_get_user_data(e);
    open_app(app);
}

void ui_all_apps_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_ENABLED);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    lv_obj_t *list1 = lv_list_create(main_page);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_center(list1);

    size_t count = 0;
    const AppEntry *apps = app_registry(&count);

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *btn = lv_list_add_btn(list1, apps[i].icon, apps[i].name);
        lv_obj_set_style_text_color(btn, THEME_COLOR_TEXT_ON_DARK, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, app_row_click_cb, LV_EVENT_CLICKED, apps[i].app);
    }

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_all_apps_exit(lv_obj_t *parent)
{
    LV_UNUSED(parent);
}

app_t ui_all_apps_main = {
    .setup_func_cb = ui_all_apps_enter,
    .exit_func_cb = ui_all_apps_exit,
    .user_data = nullptr,
};
