/**
 * @file      settings_widgets.cpp
 * @license   MIT
 * @brief     Settings row factories. See settings_widgets.h.
 */
#include "settings_widgets.h"

#include "../app_config.h"

namespace
{

/// Rows are read at arm's length on a wrist; the theme's default is sized for
/// a desktop. One font for the whole page, chosen from the panel like
/// gb_ui.cpp does.
const lv_font_t *rowFont()
{
    return lv_display_get_horizontal_resolution(NULL) <= 320 ? &lv_font_montserrat_14
                                                             : &lv_font_montserrat_18;
}

} // namespace

lv_obj_t *settings_row(lv_obj_t *parent, const char *symbol, const char *text, bool stacked)
{
    lv_obj_t *row = lv_menu_cont_create(parent);
    lv_obj_set_style_min_height(row, APP_SETTINGS_ROW_HEIGHT, 0);

    lv_obj_t *icon = NULL;
    lv_obj_t *label = NULL;

    if (symbol) {
        // A label, not lv_image: factory's create_text() takes image sources,
        // but every icon this page wants is an LV_SYMBOL_* glyph from the
        // font, which needs no asset and scales with the text.
        icon = lv_label_create(row);
        lv_label_set_text(icon, symbol);
        lv_obj_set_style_text_font(icon, rowFont(), 0);
    }

    if (text) {
        label = lv_label_create(row);
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_font(label, rowFont(), 0);
        lv_obj_set_flex_grow(label, 1);
    }

    if (stacked && icon && label) {
        // Break the flex track after the icon so the control added by the
        // caller lands on a second line, under the label rather than beside it.
        lv_obj_add_flag(icon, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(icon, label);
    }

    return row;
}

lv_obj_t *settings_slider(lv_obj_t *parent, const char *symbol, const char *text,
                          int32_t min, int32_t max, int32_t value,
                          lv_event_cb_t cb, lv_event_code_t filter, void *user_data)
{
    lv_obj_t *row = settings_row(parent, symbol, text, /*stacked*/ true);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    if (cb) {
        lv_obj_add_event_cb(slider, cb, filter, user_data);
    }
    if (!symbol) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
    return slider;
}

lv_obj_t *settings_switch(lv_obj_t *parent, const char *symbol, const char *text,
                          bool checked, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *row = settings_row(parent, symbol, text, /*stacked*/ false);

    lv_obj_t *sw = lv_switch_create(row);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    if (cb) {
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
    return sw;
}

lv_obj_t *settings_button(lv_obj_t *parent, const char *symbol, const char *text,
                          const char *button_text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *row = settings_row(parent, symbol, text, /*stacked*/ false);

    lv_obj_t *button = lv_button_create(row);
    lv_obj_set_height(button, LV_PCT(100));
    if (cb) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, button_text);
    lv_obj_set_style_text_font(label, rowFont(), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *settings_value(lv_obj_t *parent, const char *symbol, const char *text,
                         const char *value)
{
    lv_obj_t *row = settings_row(parent, symbol, text, /*stacked*/ false);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, value ? value : "");
    lv_obj_set_style_text_font(label, rowFont(), 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_GREY), 0);
    return label;
}

lv_obj_t *settings_section(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, rowFont(), 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_pad_top(label, 10, 0);
    return label;
}
