/**
 * @file      BatmanDial.ino
 * @license   MIT
 * @brief     Analog watch face with drawn hands.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/BatmanDial.
 *
 * The original shipped a bitmap dial and rotated sprite hands through TFT_eSPI.
 * There is no TFT_eSPI here and no bitmap to reuse, so the dial is drawn with
 * LVGL primitives instead: an lv_line per hand, repositioned each second from
 * the RTC.
 *
 * Trigonometry note -- screen Y grows downward and 0 degrees points right, so
 * the angle is offset by -90 degrees to put 12 o'clock at the top, and the sine
 * term is subtracted rather than added.
 *
 * Scales to either watch: the dial radius is derived from the smaller screen
 * dimension rather than being hardcoded.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <math.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

static lv_obj_t *hand_hour;
static lv_obj_t *hand_min;
static lv_obj_t *hand_sec;
static lv_obj_t *label_digital;

static lv_point_precise_t pts_hour[2];
static lv_point_precise_t pts_min[2];
static lv_point_precise_t pts_sec[2];

static int32_t centre_x, centre_y, radius;

static lv_obj_t *make_hand(lv_color_t colour, int32_t width)
{
    lv_obj_t *line = lv_line_create(lv_scr_act());
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

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);
    centre_x = w / 2;
    centre_y = h / 2;
    radius   = (w < h ? w : h) / 2 - 10;

    // Dial face
    lv_obj_t *dial = lv_obj_create(scr);
    lv_obj_set_size(dial, radius * 2, radius * 2);
    lv_obj_center(dial);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_color(dial, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_border_width(dial, 3, 0);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);

    hand_hour = make_hand(lv_color_white(), 6);
    hand_min  = make_hand(lv_color_white(), 4);
    hand_sec  = make_hand(lv_palette_main(LV_PALETTE_YELLOW), 2);

    label_digital = lv_label_create(scr);
    lv_obj_set_style_text_color(label_digital, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(label_digital, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);
}

void loop()
{
    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support T-Watch-S3 and T-Watch-Ultra"); delay(1000);
}

#endif
