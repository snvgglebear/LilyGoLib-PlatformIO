/**
 * @file      ui_clock_digital.cpp
 * @license   MIT
 * @brief     Digital clock face for the home screen.
 *
 * Ported from ui_main.cpp's original setupClock()/clock_update_datetime(),
 * minus the battery bar/label (ui_battery_status.cpp owns that now) so this
 * widget is just the HH:MM boxes, the blinking ":" and the date line.
 */
#include "ui_clock_digital.h"
#include "ui_define.h"

LV_FONT_DECLARE(font_alibaba_100);
LV_FONT_DECLARE(font_alibaba_60);
LV_FONT_DECLARE(font_alibaba_24);

/// Widgets + timer for one instance, stashed on the root object's user data so
/// destroy() can find them without a global (only one instance exists at a
/// time in practice, but this keeps the widget self-contained either way).
typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *seg;      ///< the blinking ":" between hour and minute
    lv_obj_t *date;
    lv_timer_t *timer;
} digital_clock_ctx_t;

static void tick(lv_timer_t *t)
{
    digital_clock_ctx_t *ctx = (digital_clock_ctx_t *)lv_timer_get_user_data(t);

    lv_obj_has_flag(ctx->seg, LV_OBJ_FLAG_HIDDEN) ?
    lv_obj_remove_flag(ctx->seg, LV_OBJ_FLAG_HIDDEN) :
    lv_obj_add_flag(ctx->seg, LV_OBJ_FLAG_HIDDEN);

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(ctx->hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(ctx->minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(ctx->date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
}

lv_obj_t *ui_clock_digital_create(lv_obj_t *parent)
{
    const lv_font_t *font = &font_alibaba_100;

    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_coord_t w = LV_PCT(35);
    lv_coord_t h = LV_PCT(70);
    int x_offset = 35;
    int y_offset = -20;

    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    uint32_t phy_ver_res = lv_display_get_physical_vertical_resolution(NULL);
    if (phy_ver_res > 222) {
        h = LV_PCT(45);
    }

    if (phy_hor_res == 320 && phy_ver_res == 240) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    lv_obj_t *hour_cont = lv_obj_create(page);
    lv_obj_set_size(hour_cont, w, h);
    lv_obj_align(hour_cont, LV_ALIGN_LEFT_MID, x_offset, y_offset);
    lv_obj_set_style_bg_opa(hour_cont, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hour_cont, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(hour_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_cont = lv_obj_create(page);
    lv_obj_set_size(min_cont, w, h);
    lv_obj_align(min_cont, LV_ALIGN_RIGHT_MID, -x_offset, y_offset);
    lv_obj_set_style_bg_opa(min_cont, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(min_cont, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(min_cont, LV_OBJ_FLAG_SCROLLABLE);

    digital_clock_ctx_t *ctx = (digital_clock_ctx_t *)lv_malloc(sizeof(digital_clock_ctx_t));
    memset(ctx, 0, sizeof(*ctx));

    lv_obj_t *seg = lv_label_create(page);
    lv_obj_align(seg, LV_ALIGN_CENTER, 0, -10 + y_offset);
    lv_obj_set_style_text_font(seg, font, LV_PART_MAIN);
    lv_label_set_text(seg, ":");
    lv_obj_set_style_text_color(seg, lv_color_white(), LV_PART_MAIN);
    ctx->seg = seg;

    lv_obj_t *hour = lv_label_create(hour_cont);
    lv_obj_set_style_text_font(hour, font, LV_PART_MAIN);
    lv_label_set_text(hour, "12");
    lv_obj_set_style_text_color(hour, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(hour);
    ctx->hour = hour;

    lv_obj_t *minute = lv_label_create(min_cont);
    lv_obj_set_style_text_font(minute, font, LV_PART_MAIN);
    lv_label_set_text(minute, "00");
    lv_obj_set_style_text_color(minute, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(minute);
    ctx->minute = minute;

    int offset = -5;
    if (phy_ver_res > 320) {
        offset = -45;
    }

    lv_obj_t *date = lv_label_create(page);
    lv_obj_set_style_text_font(date, &font_alibaba_24, LV_PART_MAIN);
    lv_label_set_text(date, "01-01 Thur");
    lv_obj_set_style_text_color(date, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(date, LV_ALIGN_BOTTOM_MID, 0, offset);
    ctx->date = date;

    ctx->timer = lv_timer_create(tick, 1000, ctx);
    lv_timer_ready(ctx->timer);   // paint real values immediately, not after 1 s

    lv_obj_set_user_data(page, ctx);
    return page;
}

void ui_clock_digital_destroy(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    digital_clock_ctx_t *ctx = (digital_clock_ctx_t *)lv_obj_get_user_data(obj);
    if (ctx) {
        if (ctx->timer) {
            lv_timer_del(ctx->timer);
        }
        lv_free(ctx);
    }
    lv_obj_del(obj);
}
