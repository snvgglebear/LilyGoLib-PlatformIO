/**
 * @file      ui_clock_analog.cpp
 * @license   MIT
 * @brief     Analog clock face for the clockface screen.
 *
 * Ported from examples/ui/BatmanDial/BatmanDial.ino: hands are lv_line
 * objects, repositioned each second from the RTC. The original drew directly
 * on lv_scr_act() sized to the whole panel; here the dial is sized to
 * whatever container ui_clockface_build() hands us (the clockface's own
 * tile), with centre/radius derived from that container instead of the
 * full screen.
 *
 * Trigonometry note -- screen Y grows downward and 0 degrees points right, so
 * the angle is offset by -90 degrees to put 12 o'clock at the top.
 */
#include "ui_clock_analog.h"
#include "ui_define.h"
#include "app_config.h"
#include <math.h>

typedef struct {
    lv_obj_t *hand_hour;
    lv_obj_t *hand_min;
    lv_obj_t *hand_sec;
    lv_point_precise_t pts_hour[2];
    lv_point_precise_t pts_min[2];
    lv_point_precise_t pts_sec[2];
    int32_t centre_x, centre_y, radius;
    lv_timer_t *timer;
} analog_clock_ctx_t;

static lv_obj_t *make_hand(lv_obj_t *parent, lv_color_t colour, int32_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_color(line, colour, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

/// Point a hand at `deg` degrees clockwise from 12, `len` pixels long.
static void set_hand(analog_clock_ctx_t *ctx, lv_obj_t *line, lv_point_precise_t *pts, float deg, int32_t len)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;

    pts[0].x = ctx->centre_x;
    pts[0].y = ctx->centre_y;
    pts[1].x = ctx->centre_x + (int32_t)(cosf(rad) * len);
    pts[1].y = ctx->centre_y + (int32_t)(sinf(rad) * len);

    lv_line_set_points(line, pts, 2);
}

static void tick(lv_timer_t *t)
{
    analog_clock_ctx_t *ctx = (analog_clock_ctx_t *)lv_timer_get_user_data(t);

    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    // Hour hand advances smoothly with the minutes, as a real movement does.
    float hour_deg = ((timeinfo.tm_hour % 12) + timeinfo.tm_min / 60.0f) * 30.0f;
    float min_deg  = timeinfo.tm_min * 6.0f;
    float sec_deg  = timeinfo.tm_sec * 6.0f;

    set_hand(ctx, ctx->hand_hour, ctx->pts_hour, hour_deg, ctx->radius / 2);
    set_hand(ctx, ctx->hand_min,  ctx->pts_min,  min_deg,  (ctx->radius * 3) / 4);
    set_hand(ctx, ctx->hand_sec,  ctx->pts_sec,  sec_deg,  (ctx->radius * 9) / 10);
}

lv_obj_t *ui_clock_analog_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    // The parent (a safe_area_place() band) is already given an explicit
    // pixel size, so page's LV_PCT(100) resolves as soon as layout runs.
    lv_obj_update_layout(page);
    int32_t w = lv_obj_get_width(page);
    int32_t h = lv_obj_get_height(page);
    if (w <= 0 || h <= 0) {
        // Layout has not settled yet (should not normally happen) -- fall
        // back to a small, safely visible size rather than dividing by zero.
        w = h = 120;
    }

    analog_clock_ctx_t *ctx = (analog_clock_ctx_t *)lv_malloc(sizeof(analog_clock_ctx_t));
    memset(ctx, 0, sizeof(*ctx));
    ctx->centre_x = w / 2;
    ctx->centre_y = h / 2;
    ctx->radius = (w < h ? w : h) / 2 - 10;
    if (ctx->radius < 10) {
        ctx->radius = 10;
    }

    lv_obj_t *dial = lv_obj_create(page);
    lv_obj_set_size(dial, ctx->radius * 2, ctx->radius * 2);
    lv_obj_center(dial);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, THEME_COLOR_BG_DIAL, 0);
    lv_obj_set_style_border_color(dial, THEME_COLOR_ACCENT_YELLOW, 0);
    lv_obj_set_style_border_width(dial, 3, 0);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);

    ctx->hand_hour = make_hand(page, THEME_COLOR_TEXT_ON_DARK, 6);
    ctx->hand_min  = make_hand(page, THEME_COLOR_TEXT_ON_DARK, 4);
    ctx->hand_sec  = make_hand(page, THEME_COLOR_ACCENT_YELLOW, 2);

    ctx->timer = lv_timer_create(tick, 1000, ctx);
    lv_timer_ready(ctx->timer);   // paint real hand positions immediately

    lv_obj_set_user_data(page, ctx);
    return page;
}

void ui_clock_analog_destroy(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    analog_clock_ctx_t *ctx = (analog_clock_ctx_t *)lv_obj_get_user_data(obj);
    if (ctx) {
        if (ctx->timer) {
            lv_timer_del(ctx->timer);
        }
        lv_free(ctx);
    }
    lv_obj_del(obj);
}
