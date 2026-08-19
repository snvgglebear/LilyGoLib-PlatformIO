#pragma once

/**
 * @file      settings_widgets.h
 * @license   MIT
 * @brief     Row factories for the settings menu -- one labelled control per
 *            call, all built as lv_menu_cont rows.
 *
 * A port of src/factory/ui_tools.cpp's create_text/slider/switch/button/label,
 * not an include of them: src/factory has ui_define.h/ui_tools.cpp as an
 * app-wide framework and custom_interface has no equivalent, and the two are
 * separate src_dir trees that are never compiled together.
 *
 * Two deliberate differences from the original:
 *
 *   - no lv_group_add_obj(). Factory adds every row to an input group for the
 *     T-LoRa-Pager's encoder and keyboard; custom_interface is touch-only and
 *     never creates a group. Restore them here if this app is ever built for
 *     the Pager.
 *   - no create_dropdown(). An open lv_dropdown list is a floating box sized
 *     to its options and is not routed through usable_area, so one opened near
 *     a corner of the Ultra's curved glass can render under the bezel. Every
 *     setting here is two-valued, so a switch says the same thing and cannot
 *     overflow.
 */

#include <lvgl.h>

/// Bare row: optional leading symbol, optional label, nothing else. The other
/// factories build on it. @p stacked puts the control on its own line below the
/// label (factory's LV_MENU_ITEM_BUILDER_VARIANT_2) -- what a full-width slider
/// needs so its label is not squeezed to nothing.
lv_obj_t *settings_row(lv_obj_t *parent, const char *symbol, const char *text, bool stacked);

/**
 * Labelled slider row, stacked.
 *
 * @param filter  which event invokes @p cb. LV_EVENT_VALUE_CHANGED fires
 *                continuously while dragging -- right for brightness, where the
 *                user wants live feedback; LV_EVENT_RELEASED fires once, which
 *                is what a setting that is expensive to apply should use.
 * @param user_data passed through to @p cb.
 * @return the slider, so the caller can read or update its value later.
 */
lv_obj_t *settings_slider(lv_obj_t *parent, const char *symbol, const char *text,
                          int32_t min, int32_t max, int32_t value,
                          lv_event_cb_t cb, lv_event_code_t filter, void *user_data);

/// Labelled on/off toggle. Read it in the callback with
/// lv_obj_has_state(sw, LV_STATE_CHECKED). Returns the switch.
lv_obj_t *settings_switch(lv_obj_t *parent, const char *symbol, const char *text,
                          bool checked, lv_event_cb_t cb, void *user_data);

/// Labelled action button. Returns the *button*, unlike factory's
/// create_button(), which returns the row -- callers here never want the row.
lv_obj_t *settings_button(lv_obj_t *parent, const char *symbol, const char *text,
                          const char *button_text, lv_event_cb_t cb, void *user_data);

/// Read-only "name    value" row. Returns the value label, so a periodic
/// refresh can lv_label_set_text() it.
lv_obj_t *settings_value(lv_obj_t *parent, const char *symbol, const char *text,
                         const char *value);

/// A bold heading between groups of rows within one page.
lv_obj_t *settings_section(lv_obj_t *parent, const char *text);
