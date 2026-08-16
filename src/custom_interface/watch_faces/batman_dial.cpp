/**
 * @file      batman_dial.cpp
 * @license   MIT
 * @brief     Analog watch face with drawn hands. See batman_dial.h.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/BatmanDial (by way of
 * examples/ui/BatmanDial/BatmanDial.ino).
 *
 * The original shipped a bitmap dial and rotated sprite hands through
 * TFT_eSPI. There is no TFT_eSPI here and no bitmap to reuse, so the dial is
 * drawn with LVGL primitives instead: an lv_line per hand, repositioned each
 * second from the RTC.
 *
 * Trigonometry note -- screen Y grows downward and 0 degrees points right,
 * so the angle is offset by -90 degrees to put 12 o'clock at the top, and
 * the sine term is subtracted rather than added.
 *
 * Built inside safe_area_rect(screen) rather than against the screen's raw
 * bounds, so the dial and digital readout stay clear of the curved bezel;
 * the dial radius is derived from that area's own size (not the full
 * screen), so it still scales to either watch.
 */

#include "batman_dial.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <math.h>

#include "../usable_area/usable_area.h"

static lv_obj_t *hand_hour;
static lv_obj_t *hand_min;
static lv_obj_t *hand_sec;
static lv_obj_t *label_digital;

static lv_point_precise_t pts_hour[2];
static lv_point_precise_t pts_min[2];
static lv_point_precise_t pts_sec[2];

static int32_t centre_x, centre_y, radius;

static lv_obj_t *make_hand(lv_obj_t *parent, lv_color_t colour, int32_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_color(line, colour, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

/// Point a hand at `deg` degrees clockwise from 12, `len` pixels long.
static void set_hand(lv_obj_t *line, lv_point_precise_t *pts, float deg, int32_t len)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;

    pts[0].x = centre_x;
    pts[0].y = centre_y;
    pts[1].x = centre_x + (int32_t)(cosf(rad) * len);
    pts[1].y = centre_y + (int32_t)(sinf(rad) * len);

    lv_line_set_points(line, pts, 2);
}

static void refresh(lv_timer_t *t)
{
    LV_UNUSED(t);

    RTC_DateTime now = instance.rtc.getDateTime();

    // Hour hand advances smoothly with the minutes, as a real movement does.
    float hour_deg = ((now.getHour() % 12) + now.getMinute() / 60.0f) * 30.0f;
    float min_deg  = now.getMinute()* 6.0f;
    float sec_deg  = now.getSecond() * 6.0f;

    set_hand(hand_hour, pts_hour, hour_deg, radius / 2);
    set_hand(hand_min,  pts_min,  min_deg,  (radius * 3) / 4);
    set_hand(hand_sec,  pts_sec,  sec_deg,  (radius * 9) / 10);

    lv_label_set_text_fmt(label_digital, "%02u:%02u:%02u", now.getHour(), now.getMinute(), now.getSecond());
}

static void build_face(lv_obj_t *screen)
{
    // usable_area_style_screen() already painted/clipped the screen itself;
    // build the dial inside the largest rect that's safe everywhere under
    // the curved bezel rather than against the screen's raw bounds.
    lv_obj_t *area = safe_area_rect(screen);

    int32_t w = lv_obj_get_width(area);
    int32_t h = lv_obj_get_height(area);
    centre_x = w / 2;
    centre_y = h / 2;
    radius   = (w < h ? w : h) / 2 - 10;

    // Dial face
    lv_obj_t *dial = lv_obj_create(area);
    lv_obj_set_size(dial, radius * 2, radius * 2);
    lv_obj_center(dial);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_color(dial, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_border_width(dial, 3, 0);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);

    hand_hour = make_hand(area, lv_color_white(), 6);
    hand_min  = make_hand(area, lv_color_white(), 4);
    hand_sec  = make_hand(area, lv_palette_main(LV_PALETTE_YELLOW), 2);

    label_digital = lv_label_create(area);
    lv_obj_set_style_text_color(label_digital, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(label_digital, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void batman_dial_init(lv_obj_t *screen)
{
    build_face(screen);

    // One timer at 1 Hz is enough -- the second hand only needs to jump once
    // a second, not redraw continuously.
    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);      // paint immediately rather than after 1s
}
