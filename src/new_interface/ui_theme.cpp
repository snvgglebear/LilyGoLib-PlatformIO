/**
 * @file      ui_theme.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * @brief     App-wide LVGL theme extension.
 *
 * Rather than styling each widget at its creation site, this file installs a
 * *theme extension*: LVGL's default dark theme is kept as the parent, and an
 * apply callback runs afterwards on every object as it is created, adding a few
 * extra styles by widget class.
 *
 * The reason this exists is focus visibility. On the T-LoRa-Pager there is no
 * touchscreen -- the user navigates with an encoder and keyboard -- so it must
 * always be obvious which widget currently has focus. The default theme's focus
 * cue is too subtle against this app's black background, so LV_STATE_FOCUS_KEY
 * (the "focused via keypad/encoder" state, as distinct from LV_STATE_FOCUSED)
 * is given a heavier border on the widgets that matter: dropdowns, text areas,
 * lists, and message-box buttons.
 *
 * Installed by theme_init(), called from setupGui() immediately after
 * lv_theme_default_init().
 *
 * The file is split by LVGL major version because v9 renamed the display/theme
 * accessors and reworked the message box; the intent of both halves is the same.
 *
 * @see https://docs.lvgl.io/master/details/common-widget-features/styles/index.html
 * @see https://docs.lvgl.io/master/details/other-components/theme.html
 */

#include <lvgl.h>
#include "app_config.h"
#if LVGL_VERSION_MAJOR == 9

/*Will be called when the styles of the base theme are already added
  to add new styles*/
// Runs once per object created, after the parent theme has styled it. The
// lv_obj_check_type() chain is how the callback discriminates widget types --
// there is no per-class hook in LVGL's theme API.
//
// The `static` styles are intentional and required: lv_obj_add_style() stores a
// pointer, so a stack-local style would dangle. They are re-initialised on every
// call, which is redundant but harmless.
static void new_theme_apply_cb(lv_theme_t * th, lv_obj_t * obj)
{
    static lv_style_t dropdown_style;
    lv_style_init(&dropdown_style);
    lv_style_set_border_color(&dropdown_style, THEME_COLOR_BORDER_NEUTRAL);
    lv_style_set_border_width(&dropdown_style, 1);

    static lv_style_t dropdown_focus_style;
    lv_style_init(&dropdown_focus_style);
    lv_style_set_border_color(&dropdown_focus_style, THEME_COLOR_BLACK);
    lv_style_set_border_width(&dropdown_focus_style, 2);


    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &dropdown_focus_style, LV_STATE_FOCUS_KEY);
    }

    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        static lv_style_t dropdown_focus_key_style;
        lv_style_init(&dropdown_focus_key_style);
        lv_style_set_border_color(&dropdown_focus_key_style, THEME_COLOR_BLACK);
        lv_style_set_border_width(&dropdown_focus_key_style, 2);
        lv_style_set_bg_color(&dropdown_focus_key_style, THEME_COLOR_BLACK);
        lv_style_set_text_color(&dropdown_focus_key_style, THEME_COLOR_TEXT_ON_DARK);

        lv_obj_add_style(obj, &dropdown_style, LV_PART_MAIN );
        lv_obj_add_style(obj, &dropdown_style, LV_STATE_DEFAULT);
        lv_obj_add_style(obj, &dropdown_focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    }

    if (lv_obj_check_type(obj, &lv_list_class)) {

        static lv_style_t list_bg;
        lv_style_init(&list_bg);
        lv_style_set_border_width(&list_bg, 0);
        lv_obj_add_style(obj, &list_bg, 0);
    }

   
}

/**
 * Install the theme extension on the default display.
 *
 * Copies the active theme, chains it as the new theme's parent, and attaches
 * new_theme_apply_cb(). Chaining rather than replacing means the default dark
 * theme still does all the base styling and this file only adds to it.
 *
 * `th_new` must be static -- LVGL keeps a pointer to it for the lifetime of the
 * display.
 *
 * Must be called after lv_theme_default_init() and before any widget is created,
 * since the callback only runs for objects created after it is installed.
 */
void theme_init()
{
    /*Initialize the new theme from the current theme*/
    lv_theme_t *th_act = lv_display_get_theme(NULL);
    static lv_theme_t th_new;
    th_new = *th_act;

    /*Set the parent theme and the style apply callback for the new theme*/
    lv_theme_set_parent(&th_new, th_act);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);

    /*Assign the new theme to the current display*/
    lv_display_set_theme(NULL, &th_new);
}

#else

static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    static  lv_style_t outline_primary;
    static lv_style_t outline_secondary;
    lv_style_init(&outline_primary);
    lv_style_set_outline_opa(&outline_primary, LV_OPA_50);

    lv_style_init(&outline_secondary);
    lv_style_set_outline_opa(&outline_secondary, LV_OPA_50);

    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_border_color(&btn_style, THEME_COLOR_BLACK);
    lv_style_set_border_width(&btn_style, 1);
    lv_style_set_bg_color(&btn_style, THEME_COLOR_BLACK);

    static lv_style_t btn_focus_style;
    lv_style_init(&btn_focus_style);
    lv_style_set_border_color(&btn_focus_style, THEME_COLOR_BLACK);
    lv_style_set_border_width(&btn_focus_style, 2);

    if (lv_obj_check_type(obj, &lv_label_class)) {


    } else if (lv_obj_check_type(obj, &lv_btnmatrix_class)) {

        if (lv_obj_check_type(lv_obj_get_parent(obj), &lv_msgbox_class)) {

            lv_obj_add_style(obj, &btn_focus_style, LV_STATE_FOCUS_KEY | LV_STATE_CHECKED);
            lv_obj_add_style(obj, &btn_focus_style, LV_PART_ITEMS | LV_STATE_DISABLED);
            lv_obj_add_style(obj, &btn_focus_style, LV_PART_ITEMS | LV_STATE_CHECKED);
            lv_obj_add_style(obj, &btn_focus_style, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
            lv_obj_add_style(obj, &btn_focus_style, LV_PART_ITEMS | LV_STATE_EDITED);
            return;
        }

    } else if (lv_obj_check_type(obj, &lv_msgbox_class)) {

        static lv_style_t msgbox_bg;
        lv_style_init(&msgbox_bg);
        lv_style_set_border_color(&msgbox_bg, THEME_COLOR_BLACK);
        lv_style_set_border_width(&msgbox_bg, 2);
        lv_obj_add_style(obj, &msgbox_bg, 0);


        static lv_style_t bg_color_primary;
        lv_style_init(&bg_color_primary);
        lv_style_set_bg_color(&bg_color_primary, THEME_COLOR_BLACK);
        lv_style_set_text_color(&bg_color_primary, THEME_COLOR_TEXT_ON_DARK);
        lv_style_set_bg_opa(&bg_color_primary, LV_OPA_80);
        lv_obj_add_style(obj, &bg_color_primary, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &bg_color_primary, LV_STATE_PRESSED);

        lv_obj_add_style(obj, &bg_color_primary, 0);


    } else if (lv_obj_check_type(obj, &lv_msgbox_backdrop_class)) {


    } else if (lv_obj_check_type(obj, &lv_btn_class)) {

        lv_obj_add_style(obj, &btn_style, LV_PART_MAIN);
        lv_obj_add_style(obj, &btn_focus_style, LV_STATE_FOCUSED);

    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {

        static lv_style_t dropdown_style;
        lv_style_init(&dropdown_style);
        lv_style_set_border_color(&dropdown_style, THEME_COLOR_BORDER_NEUTRAL);
        lv_style_set_border_width(&dropdown_style, 1);

        static lv_style_t dropdown_focus_style;
        lv_style_init(&dropdown_focus_style);
        lv_style_set_border_color(&dropdown_focus_style, THEME_COLOR_BLACK);
        lv_style_set_border_width(&dropdown_focus_style, 2);

        static lv_style_t dropdown_focus_key_style;
        lv_style_init(&dropdown_focus_key_style);
        lv_style_set_border_color(&dropdown_focus_key_style, THEME_COLOR_BLACK);
        lv_style_set_border_width(&dropdown_focus_key_style, 2);
        lv_style_set_bg_color(&dropdown_focus_key_style, THEME_COLOR_BLACK);
        lv_style_set_text_color(&dropdown_focus_key_style, THEME_COLOR_TEXT_ON_DARK);

        lv_obj_add_style(obj, &dropdown_style, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_style(obj, &dropdown_focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {

        static lv_style_t s_style_bg_color;
        static lv_style_t s_style_focus_bg_color;

        lv_style_init(&s_style_bg_color);
        lv_style_set_bg_color(&s_style_bg_color, THEME_COLOR_BLACK);
        lv_style_set_bg_opa(&s_style_bg_color, 255);

        lv_obj_add_style(obj, &s_style_bg_color, LV_PART_SELECTED | LV_STATE_CHECKED);

    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {

        static lv_style_t textarea_style_bg_color;

        lv_style_init(&textarea_style_bg_color);
        lv_style_set_border_color(&textarea_style_bg_color, THEME_COLOR_BLACK);
        lv_style_set_border_width(&textarea_style_bg_color, 2);
        lv_obj_add_style(obj, &textarea_style_bg_color, LV_STATE_FOCUS_KEY);


        static lv_style_t textarea_style_focus_bg_color;
        lv_style_init(&textarea_style_focus_bg_color);
        lv_style_set_shadow_width(&textarea_style_focus_bg_color, 2);
        lv_style_set_shadow_color(&textarea_style_focus_bg_color, THEME_COLOR_ACCENT_BLUE);
        lv_style_set_outline_color(&textarea_style_focus_bg_color, THEME_COLOR_ACCENT_BLUE);
        lv_style_set_outline_opa(&textarea_style_focus_bg_color, LV_OPA_100);
        lv_obj_add_style(obj, &textarea_style_focus_bg_color, LV_STATE_EDITED);
        lv_obj_add_style(obj, &textarea_style_focus_bg_color, LV_STATE_FOCUS_KEY);

    } else if (lv_obj_check_type(obj, &lv_menu_cont_class)) {

        static lv_style_t menu_page_sel_style;
        lv_style_init(&menu_page_sel_style);
        lv_style_set_bg_color(&menu_page_sel_style, THEME_COLOR_BLACK);
        lv_style_set_text_color(&menu_page_sel_style, THEME_COLOR_TEXT_ON_DARK);
        lv_style_set_border_color(&menu_page_sel_style, THEME_COLOR_BLACK);
        lv_style_set_border_width(&menu_page_sel_style, 1);
        lv_style_set_radius(&menu_page_sel_style, 10);

        lv_obj_add_style(obj, &menu_page_sel_style, LV_STATE_PRESSED);
        lv_obj_add_style(obj, &menu_page_sel_style, LV_STATE_PRESSED | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &menu_page_sel_style, LV_STATE_CHECKED);
        lv_obj_add_style(obj, &menu_page_sel_style, LV_STATE_FOCUS_KEY);

    } else if (lv_obj_check_type(obj, &lv_menu_page_class)) {


        static lv_style_t menu_page_default_style;
        lv_style_init(&menu_page_default_style);
        lv_style_set_border_color(&menu_page_default_style, THEME_COLOR_ACCENT_PURPLE);
        lv_style_set_border_width(&menu_page_default_style, 0);
        lv_style_set_radius(&menu_page_default_style, 10);
        lv_obj_add_style(obj,  &menu_page_default_style, 0);


        static lv_style_t menu_page_focus_style;
        lv_style_init(&menu_page_focus_style);
        lv_style_set_border_color(&menu_page_focus_style, THEME_COLOR_BLACK);
        lv_style_set_border_width(&menu_page_focus_style, 1);
        lv_style_set_radius(&menu_page_focus_style, 10);
        lv_obj_add_style(obj,  &menu_page_focus_style, LV_PART_MAIN | LV_STATE_FOCUSED);

    } else if (lv_obj_check_type(obj, &lv_list_btn_class)) {

        static lv_style_t bg_color_primary;
        lv_style_init(&bg_color_primary);
        lv_style_set_bg_color(&bg_color_primary, THEME_COLOR_BLACK);
        lv_style_set_text_color(&bg_color_primary, THEME_COLOR_TEXT_ON_DARK);
        lv_style_set_bg_opa(&bg_color_primary, LV_OPA_COVER);
        lv_obj_add_style(obj, &bg_color_primary, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &bg_color_primary, LV_STATE_PRESSED);

    }   else if (lv_obj_check_type(obj, &lv_list_class)) {

        static lv_style_t list_bg;
        lv_style_init(&list_bg);
        lv_style_set_border_width(&list_bg, 0);
        lv_obj_add_style(obj, &list_bg, 0);
    }
}

void theme_init()
{
    lv_theme_t *th_active = lv_theme_get_from_obj(NULL);
    static lv_theme_t th_new = *th_active;
    lv_theme_set_parent(&th_new, th_active);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);
    lv_disp_set_theme(NULL, &th_new);
}
#endif

