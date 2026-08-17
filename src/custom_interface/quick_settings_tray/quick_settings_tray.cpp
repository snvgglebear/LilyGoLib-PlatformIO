/**
 * @file      quick_settings_tray.cpp
 * @license   MIT
 * @brief     Swipe-down quick-settings shade. See quick_settings_tray.h.
 */
#include "quick_settings_tray.h"

#include "quick_settings_tray_hal.h"
#include <usable_area.h>

namespace
{

constexpr int32_t QST_TRAY_HEIGHT       = 230;
constexpr int32_t QST_HEADER_HEIGHT     = 110;
constexpr int32_t QST_BRIGHTNESS_HEIGHT = 70;
constexpr int32_t QST_FOOTER_HEIGHT     = 50;
constexpr uint32_t QST_ANIM_DURATION_MS = 220;

/// Mirrors the three-way state a bool can't represent cleanly: an in-flight
/// open/close anim still needs its target (OPEN/CLOSED) to know which way an
/// interrupting call should go, and open()/close() both no-op except from a
/// stable or opposite-direction state.
enum QstState {
    QST_CLOSED,
    QST_OPENING,
    QST_OPEN,
    QST_CLOSING,
};

QstState s_state = QST_CLOSED;

lv_obj_t *s_scrim = nullptr;
lv_obj_t *s_tray = nullptr;

lv_obj_t *s_time_label = nullptr;
lv_obj_t *s_date_label = nullptr;
lv_obj_t *s_batt_icon = nullptr;
lv_obj_t *s_batt_bar = nullptr;
lv_obj_t *s_batt_pct_label = nullptr;
lv_obj_t *s_brightness_slider = nullptr;
lv_obj_t *s_brightness_pct_label = nullptr;

void quick_settings_tray_close(void);

const char *batteryIconFor(int percent)
{
    if (percent >= 80) return LV_SYMBOL_BATTERY_FULL;
    if (percent >= 60) return LV_SYMBOL_BATTERY_3;
    if (percent >= 40) return LV_SYMBOL_BATTERY_2;
    if (percent >= 20) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

void refreshContent()
{
    QstTimeDate td;
    qst_hal_get_time_date(&td);
    lv_label_set_text(s_time_label, td.time);
    lv_label_set_text(s_date_label, td.date);

    QstBattery batt;
    qst_hal_get_battery(&batt);
    lv_bar_set_value(s_batt_bar, batt.percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_batt_pct_label, "%d%%", batt.percent);
    lv_label_set_text(s_batt_icon, batt.charging ? LV_SYMBOL_CHARGE : batteryIconFor(batt.percent));

    int min = qst_hal_brightness_min();
    int max = qst_hal_brightness_max();
    int level = qst_hal_get_brightness();
    lv_slider_set_value(s_brightness_slider, level, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_brightness_pct_label, "%d%%", (level - min) * 100 / (max - min));
}

void trayAnimYCb(void *obj, int32_t v)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), v);
}

void trayOpenDoneCb(lv_anim_t *a)
{
    LV_UNUSED(a);
    s_state = QST_OPEN;
}

void trayCloseDoneCb(lv_anim_t *a)
{
    LV_UNUSED(a);
    lv_obj_add_flag(s_tray, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    s_state = QST_CLOSED;
}

void scrimClickCb(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_settings_tray_close();
}

void grabberClickCb(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_settings_tray_close();
}

/// Closes on an upward swipe (LV_DIR_TOP: finger moves toward the top of the
/// screen) starting on the tray itself -- the mirror of the open gesture the
/// app wires up on non-watchface screens.
void trayGestureCb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
        quick_settings_tray_close();
    }
}

void brightnessSliderCb(lv_event_t *e)
{
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int level = lv_slider_get_value(slider);
    qst_hal_set_brightness(level);

    int min = qst_hal_brightness_min();
    int max = qst_hal_brightness_max();
    lv_label_set_text_fmt(s_brightness_pct_label, "%d%%", (level - min) * 100 / (max - min));
}

lv_obj_t *makeBand(lv_obj_t *tray, int32_t y, int32_t height)
{
    // usable_area_place() can return NULL for a band entirely inside the
    // bezel -- not expected for anything inside a 230px tray dropped from
    // the top of a 502px-tall Ultra panel, but fall back to the tray itself
    // rather than crash if the geometry ever changes.
    lv_obj_t *band = usable_area_place(tray, y, height);
    return band ? band : tray;
}

void buildHeader(lv_obj_t *tray)
{
    lv_obj_t *band = makeBand(tray, 0, QST_HEADER_HEIGHT);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *time_col = lv_obj_create(band);
    lv_obj_remove_style_all(time_col);
    lv_obj_set_size(time_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(time_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_time_label = lv_label_create(time_col);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_time_label, "--:--");

    s_date_label = lv_label_create(time_col);
    lv_obj_set_style_text_color(s_date_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_date_label, "");

    lv_obj_t *batt_col = lv_obj_create(band);
    lv_obj_remove_style_all(batt_col);
    lv_obj_set_size(batt_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(batt_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(batt_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(batt_col, 4, 0);

    s_batt_icon = lv_label_create(batt_col);
    lv_obj_set_style_text_color(s_batt_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_batt_icon, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_batt_icon, LV_SYMBOL_BATTERY_FULL);

    s_batt_bar = lv_bar_create(batt_col);
    lv_obj_set_size(s_batt_bar, 80, 12);
    lv_bar_set_range(s_batt_bar, 0, 100);

    s_batt_pct_label = lv_label_create(batt_col);
    lv_obj_set_style_text_color(s_batt_pct_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_batt_pct_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_batt_pct_label, "--%");
}

void buildBrightnessRow(lv_obj_t *tray)
{
    lv_obj_t *band = makeBand(tray, QST_HEADER_HEIGHT, QST_BRIGHTNESS_HEIGHT);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // LVGL's built-in symbol font has no sun/brightness glyph; the "image"
    // symbol is the closest stand-in for "the display" among what exists.
    lv_obj_t *icon = lv_label_create(band);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_label_set_text(icon, LV_SYMBOL_IMAGE);

    s_brightness_slider = lv_slider_create(band);
    lv_obj_set_width(s_brightness_slider, LV_PCT(50));
    lv_slider_set_range(s_brightness_slider, qst_hal_brightness_min(), qst_hal_brightness_max());
    lv_obj_add_event_cb(s_brightness_slider, brightnessSliderCb, LV_EVENT_VALUE_CHANGED, NULL);

    s_brightness_pct_label = lv_label_create(band);
    lv_obj_set_style_text_color(s_brightness_pct_label, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_brightness_pct_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_brightness_pct_label, "--%");
}

void buildFooter(lv_obj_t *tray)
{
    lv_obj_t *band = makeBand(tray, QST_HEADER_HEIGHT + QST_BRIGHTNESS_HEIGHT, QST_FOOTER_HEIGHT);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *grabber = lv_label_create(band);
    lv_obj_set_style_text_color(grabber, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(grabber, &lv_font_montserrat_20, 0);
    lv_label_set_text(grabber, LV_SYMBOL_UP);
    lv_obj_add_flag(grabber, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(grabber, grabberClickCb, LV_EVENT_CLICKED, NULL);
}

void quick_settings_tray_close(void)
{
    if (s_state == QST_CLOSED || s_state == QST_CLOSING) {
        return;
    }
    s_state = QST_CLOSING;
    lv_display_trigger_activity(NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_tray);
    lv_anim_set_values(&a, lv_obj_get_y(s_tray), -QST_TRAY_HEIGHT);
    lv_anim_set_duration(&a, QST_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, trayAnimYCb);
    lv_anim_set_completed_cb(&a, trayCloseDoneCb);
    lv_anim_start(&a);

    lv_indev_wait_release(lv_indev_active());
}

} // namespace

void quick_settings_tray_init(void)
{
    int32_t w = usable_area_screen_width();
    int32_t h = usable_area_screen_height();

    lv_obj_t *top = lv_layer_top();

    s_scrim = lv_obj_create(top);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, w, h);
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_50, 0);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scrim, scrimClickCb, LV_EVENT_CLICKED, NULL);

    s_tray = lv_obj_create(top);
    lv_obj_remove_flag(s_tray, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_tray, w, QST_TRAY_HEIGHT);
    lv_obj_set_pos(s_tray, 0, -QST_TRAY_HEIGHT);
    lv_obj_set_style_bg_color(s_tray, lv_color_hex(0x1c1c1c), 0);
    lv_obj_set_style_bg_opa(s_tray, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tray, 0, 0);
    lv_obj_set_style_pad_all(s_tray, 0, 0);
    lv_obj_set_style_radius(s_tray, 0, 0);
    lv_obj_add_flag(s_tray, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_tray, trayGestureCb, LV_EVENT_GESTURE, NULL);

    buildHeader(s_tray);
    buildBrightnessRow(s_tray);
    buildFooter(s_tray);
}

void quick_settings_tray_open(void)
{
    if (s_state == QST_OPEN || s_state == QST_OPENING) {
        return;
    }
    s_state = QST_OPENING;

    refreshContent();
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_tray, LV_OBJ_FLAG_HIDDEN);
    lv_display_trigger_activity(NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_tray);
    lv_anim_set_values(&a, lv_obj_get_y(s_tray), 0);
    lv_anim_set_duration(&a, QST_ANIM_DURATION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, trayAnimYCb);
    lv_anim_set_completed_cb(&a, trayOpenDoneCb);
    lv_anim_start(&a);

    lv_indev_wait_release(lv_indev_active());
}
