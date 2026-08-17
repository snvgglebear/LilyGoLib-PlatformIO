/**
 * @file      ui_tools.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * @brief     Themed widget factories shared by every app screen.
 *
 * Implements the `create_*` / `ui_create_*` helpers declared in ui_define.h.
 * Apps build their screens from these rather than raw lv_*_create() calls, which
 * gives three things for free: consistent styling, correct sizing across the
 * three very different panel resolutions, and automatic membership of the active
 * input group so the encoder and keyboard can reach every control.
 *
 * Most helpers produce one row of a settings-style menu: a caption on the left
 * and a control (slider, switch, dropdown, ...) on the right.
 *
 * Note the recurring `#if LVGL_VERSION_MAJOR == 8` blocks -- LVGL 9's default
 * theme already provides much of the styling that had to be applied by hand
 * under v8, and several APIs were renamed. See the compatibility shim at the
 * bottom of ui_define.h.
 *
 * @see https://docs.lvgl.io/master/details/widgets/index.html
 * @see https://docs.lvgl.io/master/details/common-widget-features/styles/index.html
 */
#include "ui_define.h"
#include "app_config.h"
#include <usable_area.h>

// Modal dialog focus handling: while a message box is up, input is confined to
// `msg_group` so the user cannot tab into the screen behind it. `prev_group`
// remembers what was focused before, to be restored by destroy_msgbox().
static lv_group_t *msg_group = NULL;
static lv_group_t *prev_group;


/**
 * Full-screen progress view: a bar with a caption underneath, for operations
 * with a knowable duration (WiFi scan, file transfer, firmware check).
 *
 * @return the bar object -- update it with lv_bar_set_value(). The containing
 *         panel is stashed in the bar's user data, so callers can delete the
 *         whole view with lv_obj_delete(lv_obj_get_user_data(bar)).
 */
lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title)
{
#if LVGL_VERSION_MAJOR == 8
    static lv_style_t style_bg;
    static lv_style_t style_indic;

    lv_style_init(&style_bg);
    lv_style_set_border_color(&style_bg, THEME_COLOR_BLACK);
    lv_style_set_border_width(&style_bg, 2);
    lv_style_set_pad_all(&style_bg, 6);
    lv_style_set_radius(&style_bg, 6);
    lv_style_set_anim_time(&style_bg, 1000);
    lv_style_init(&style_indic);
    lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&style_indic, THEME_COLOR_BG_DARK);
    lv_style_set_radius(&style_indic, 3);


    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 5);

    /*Make a gradient*/
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    static lv_grad_dsc_t grad;
    grad.dir = LV_GRAD_DIR_VER;
    grad.stops_count = 2;
    grad.stops[0].color = THEME_COLOR_PROGRESS_GRADIENT_START;
    grad.stops[1].color = THEME_COLOR_PROGRESS_GRADIENT_END;

    /*Shift the gradient to the bottom*/
    grad.stops[0].frac  = 128;
    grad.stops[1].frac  = 192;

    lv_style_set_bg_grad(&style, &grad);
#endif

    /*Create an object with the new style*/
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
#if LVGL_VERSION_MAJOR == 8
    lv_obj_add_style(cont, &style, 0);
#endif
    lv_obj_center(cont);

    lv_obj_t *bar = lv_bar_create(cont);
#if LVGL_VERSION_MAJOR == 8
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, &style_bg, 0);
    lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);
#endif
    lv_obj_set_size(bar, 200, 20);
    lv_obj_center(bar);
    lv_obj_set_user_data(bar, cont);
    lv_bar_set_value(bar, 0, LV_ANIM_ON);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
#if LVGL_VERSION_MAJOR == 8
    lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_ON_DARK, LV_PART_MAIN);
#endif
    lv_obj_align_to(label, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    return bar;
}

/**
 * Generic settings row: caption + a caller-supplied control + an optional
 * trailing action button.
 *
 * The `widget_create` callback lets the caller drop in any widget while this
 * function keeps ownership of the layout, so rows built from different control
 * types still line up. Widths are percentages rather than pixels for the same
 * reason -- the identical row has to work on a 240 px watch and a 480 px pager.
 *
 * @param symbol_txt     caption for the trailing button (usually an LV_SYMBOL_*),
 *                       or NULL for no button -- in which case the main control
 *                       widens from 55% to 65% to take up the freed space
 * @param widget_create  builds the control; receives the row container as parent
 * @param btn_event_cb   click handler for the trailing button; may be NULL
 * @return the row container
 */
lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb)
{
    lv_obj_t *cont;
    lv_obj_t *label;
    lv_obj_t *obj;
    lv_obj_t *btn;
    cont = lv_menu_cont_create(parent);

    label = lv_label_create(cont);
    lv_obj_set_width(label, lv_pct(25));

    lv_label_set_text(label, title);
    obj = widget_create(cont);
    if (symbol_txt) {
        lv_obj_set_size(obj, lv_pct(55), 40);
    } else {
        lv_obj_set_size(obj, lv_pct(65), 40);
    }
    lv_obj_set_style_outline_color(obj, THEME_COLOR_TEXT_ON_DARK, LV_STATE_FOCUS_KEY);

    if (symbol_txt) {
        btn = lv_btn_create(cont);
        if (btn_event_cb) {
            lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_set_size(btn, lv_pct(12), 40);
        label = lv_label_create(btn);
        lv_obj_center(label);
        lv_label_set_text(label, symbol_txt);
    }
    return cont;
}

/**
 * Close a dialog created by create_msgbox() and give focus back to the screen
 * underneath. Every dialog must be torn down through here, not with a bare
 * lv_msgbox_close() -- otherwise the input devices stay pointed at `msg_group`
 * and the UI becomes unnavigable.
 */
void destroy_msgbox(lv_obj_t *msgbox)
{
#if LVGL_VERSION_MAJOR == 9
#else
    // v8 only: hide the button matrix first, so a queued click cannot be
    // delivered to buttons that are about to be freed.
    lv_obj_t *msg_btns = lv_msgbox_get_btns(msgbox);
    lv_group_focus_obj(msg_btns);
    lv_btnmatrix_set_btn_ctrl_all(msg_btns, LV_BTNMATRIX_CTRL_HIDDEN);
#endif
    lv_msgbox_close(msgbox);
    // lv_group_focus_freeze(msg_group, false);
    set_default_group(prev_group);
}

/**
 * Modal dialog with a title, a body, and a row of buttons.
 *
 * Focus is captured before the dialog is built (`prev_group` is saved, input
 * redirected to `msg_group`) and released again in destroy_msgbox(). That pair
 * is what makes the dialog modal for the encoder and keyboard, not just visually.
 *
 * The v8 and v9 branches differ substantially because LVGL 9 rebuilt the message
 * box widget: v9 composes it from real footer buttons that each get their own
 * event callback, whereas v8 uses a single button matrix and one callback on the
 * dialog. Callers see the same interface either way.
 *
 * @param parent          unused -- both branches create the dialog on the active
 *                        screen (passing NULL to lv_msgbox_create)
 * @param btns            array of captions, terminated by NULL or an empty string
 * @param btns_event_cb   click handler; `user_data` is forwarded to it
 * @return the dialog, to be passed to destroy_msgbox()
 * @see    https://docs.lvgl.io/master/details/widgets/msgbox.html
 */
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data)
{

    prev_group = lv_group_get_default();

    if (!msg_group) {
        msg_group = lv_group_create();
    }
    set_default_group(msg_group);

    lv_obj_t *msgbox;

#if LVGL_VERSION_MAJOR == 9
    msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_text(msgbox, msg_txt);
    lv_obj_set_size(msgbox, lv_pct(90), lv_pct(60));
    uint32_t btn_cnt = 0;
    lv_obj_t *btn;
    // Walk the caption array until the NULL/empty terminator, adding each button
    // to the modal focus group as it goes. The loop leaves `btn` pointing at the
    // last button, which is then given initial focus.
    while (btns[btn_cnt] && btns[btn_cnt][0] != '\0') {
        btn = lv_msgbox_add_footer_button(msgbox, btns[btn_cnt]);
        lv_obj_add_event_cb(btn, btns_event_cb, LV_EVENT_CLICKED, user_data);
        lv_group_add_obj(msg_group, btn);
        btn_cnt++;
    }
    lv_group_focus_obj(btn);
    lv_obj_add_state(btn, LV_STATE_FOCUS_KEY);

#else
    msgbox = lv_msgbox_create(NULL, title_txt, " ", btns, false);
    lv_msgbox_t *mbox = (lv_msgbox_t *)msgbox;
    lv_label_set_text_fmt(mbox->text, msg_txt);
    lv_label_set_long_mode(mbox->text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mbox->text, lv_pct(100));

    lv_obj_t *content = lv_msgbox_get_text(msgbox);
    lv_obj_set_style_text_font(content, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(content, THEME_COLOR_TEXT_ON_DARK, LV_PART_MAIN);

    lv_obj_t *msg_btns = lv_msgbox_get_btns(msgbox);
    lv_btnmatrix_set_btn_ctrl(msg_btns, 0, LV_BTNMATRIX_CTRL_CHECKED);

    lv_group_focus_obj(msg_btns);

    lv_obj_set_size(msgbox, lv_pct(90), lv_pct(60));
    lv_obj_set_style_radius(msgbox, 30, LV_PART_MAIN);

    lv_obj_set_style_bg_color(msgbox, THEME_COLOR_BG_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(msgbox, LV_OPA_60, LV_PART_MAIN);

    lv_obj_t *title = lv_msgbox_get_title(msgbox);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, THEME_COLOR_TEXT_ON_DARK, LV_PART_MAIN);

    lv_obj_add_event_cb(msgbox, btns_event_cb, LV_EVENT_CLICKED, user_data);

    lv_obj_center(msgbox);
#endif

    return msgbox;

}

/**
 * The base row every other helper builds on: an optional icon plus a label,
 * inside a flex container.
 *
 * The label uses LV_LABEL_LONG_SCROLL_CIRCULAR, so captions too long for the
 * narrow watch screens scroll horizontally rather than being clipped.
 *
 * `builder_variant` controls the arrangement:
 *   - VARIANT_1: icon and text side by side on one line.
 *   - VARIANT_2: the icon is pushed to a new flex track and swapped ahead of the
 *     label, giving a stacked layout with room for a control beneath -- which is
 *     why create_slider() and friends use this variant.
 */
lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant)
{
    lv_obj_t *obj = lv_menu_cont_create(parent);

    lv_obj_t *img = NULL;
    lv_obj_t *label = NULL;

    if (icon) {
        img = lv_img_create(obj);
        lv_img_set_src(img, icon);
    }

    if (txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if (builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

/**
 * Labelled slider row.
 *
 * @param filter  which LVGL event invokes `cb`. LV_EVENT_VALUE_CHANGED fires
 *                continuously while dragging -- right for brightness, where the
 *                user wants live feedback; LV_EVENT_RELEASED fires once on
 *                release, which is what settings that are expensive to apply
 *                (charge current, radio retune) use instead.
 * @return the slider, so callers can read or update its value later
 */
lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t *slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);

    if (cb != NULL) {
        lv_obj_add_event_cb(slider, cb, filter, NULL);
    }

    if (icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return slider;
}

/// Labelled on/off toggle. Returns the switch; read its state with
/// lv_obj_has_state(sw, LV_STATE_CHECKED) inside the callback.
/// @note `cb` is not NULL-checked here, unlike the other helpers.
lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);

    lv_obj_t *sw = lv_switch_create(obj);
    lv_obj_add_state(sw, chk ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

/// Labelled action button. Note this returns the *row*, not the button -- unlike
/// create_slider()/create_switch(), which return the control itself.
lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_t *btn = lv_btn_create(obj);
    lv_obj_set_size(btn, lv_pct(10), lv_pct(100));
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return obj;
}

/// Read-only "name: value" row for status displays.
/// @return the value label when `default_text` is given (update it with
///         lv_label_set_text), otherwise the row container.
lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    if (default_text) {
        lv_obj_t *label = lv_label_create(obj);
        lv_label_set_text(label, default_text);
        return label;
    }
    return obj;
}

/**
 * Labelled dropdown row.
 * @param options  LVGL's option format: a single newline-separated string, e.g.
 *                 "433MHz\n868MHz\n915MHz". The radio drivers' radio_get_*_list()
 *                 functions return strings in exactly this form, paired with a
 *                 lookup array indexed by the selection this widget reports.
 */
lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb)
{
    lv_obj_t *obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_1);
    lv_obj_t *dd = lv_dropdown_create(obj);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, default_sel);
    if (cb) {
        lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    return dd;
}



static void float_button_event_cb(lv_event_t * e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
}

/**
 * The floating back button, pinned near the bottom of the active screen and
 * drawn above everything else (LV_OBJ_FLAG_FLOATING keeps it out of the parent's
 * scrollable content, so it does not move when the screen scrolls).
 *
 * This is how nearly every app is closed: the handler passed in typically calls
 * the app's own exit_func_cb and then menu_show().
 *
 * FLOAT_BUTTON_WIDTH/HEIGHT come from the per-board block in hal_interface.h --
 * 80 px on the high-resolution Ultra, 40 px elsewhere -- so the touch target
 * stays a comfortable physical size across the different panel densities.
 */
lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data)
{
    lv_obj_t *float_btn = lv_btn_create(lv_screen_active());
    lv_obj_set_size(float_btn, FLOAT_BUTTON_WIDTH, FLOAT_BUTTON_HEIGHT);
    lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(float_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_event_cb(float_btn, event_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_image_src(float_btn, LV_SYMBOL_LEFT, 0);
    lv_obj_set_style_text_font(float_btn, lv_theme_get_font_large(float_btn), 0);
    return float_btn;
}


/// Circular icon button. Same shape as the floating back button, but created on
/// a caller-supplied parent and positioned by the caller, with an arbitrary
/// image instead of the fixed back arrow.
lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data)
{
    lv_obj_t *float_btn = lv_btn_create(parent);
    lv_obj_set_size(float_btn, FLOAT_BUTTON_WIDTH, FLOAT_BUTTON_HEIGHT);
    lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
    // lv_obj_align(float_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_event_cb(float_btn, event_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, 0);
    if (image) {
        lv_obj_set_style_bg_image_src(float_btn, image, 0);
    }
    lv_obj_set_style_text_font(float_btn, lv_theme_get_font_large(float_btn), 0);
    return float_btn;
}

/**
 * Full-screen scrollable settings menu, the container for create_slider() and
 * the other row helpers.
 *
 * On boards without a touchscreen the menu widget's own root back button is
 * enabled, since there is no swipe gesture to go back with; touch boards use the
 * floating back button instead and would otherwise show two.
 *
 * Sized to the safe-area rectangle (usable_area.h) rather than the full parent
 * rect: at LV_PCT(100) the menu's header bar and top/bottom rows ran under the
 * T-Watch-Ultra's curved bezel, where the screen root clips them away. Resizing
 * the menu object itself (instead of wrapping it in a usable_area_rect() child)
 * keeps every existing `lv_obj_del(menu)` call site correct with no orphaned
 * container left behind.
 */
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb)
{
    lv_obj_t *menu = lv_menu_create(parent);
#ifndef USING_TOUCHPAD
    lv_menu_set_mode_root_back_btn(menu, LV_MENU_ROOT_BACK_BTN_ENABLED);
#endif
    lv_obj_add_event_cb(menu, event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(menu, usable_area_screen_width() - 2 * SAFE_INSET, usable_area_screen_height() - 2 * SAFE_INSET);
    lv_obj_center(menu);
    return menu;
}

// Emulator-only implementations: on hardware, LilyGoLib registers the input
// devices and provides these accessors itself. Here they search LVGL's device
// list for the SDL mouse-wheel (standing in for the encoder) and SDL keyboard.
#ifndef ARDUINO
lv_indev_t *lv_get_encoder_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            return indev;
        }
        indev = lv_indev_get_next(indev);
    }
    return NULL;
}


lv_indev_t *lv_get_keyboard_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
        indev = lv_indev_get_next(indev);
    }
    return NULL;
}
#endif

void disable_input_devices()
{
    hw_disable_input_devices();
    lv_indev_enable(lv_get_encoder_indev(), false);
    if (hw_has_keyboard()) {
        lv_indev_enable(lv_get_keyboard_indev(), false);
    }
}

void enable_input_devices()
{
    hw_enable_input_devices();
    lv_indev_enable(lv_get_encoder_indev(), true);
    if (hw_has_keyboard()) {
        lv_indev_enable(lv_get_keyboard_indev(), true);
    }
}

void disable_keyboard()
{
    if (hw_has_keyboard()) {
        hw_disable_keyboard();
        lv_indev_enable(lv_get_keyboard_indev(), false);
    }
}

void enable_keyboard()
{
    if (hw_has_keyboard()) {
        hw_enable_keyboard();
        hw_flush_keyboard();
        lv_indev_enable(lv_get_keyboard_indev(), true);
    }
}

bool is_screen_small()
{
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    printf("Screen size: %dx%d\n", w, h);
    if (w <= 240 || h <= 240) {
        printf("Small screen detected.\n");
        return true;
    }
    return false;
}