/**
 * @file      simple_face.cpp
 * @license   MIT
 * @brief     A minimal but complete watch face. See simple_face.h.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/SimpleWatch.
 *
 * The original was written for a 240x240 TFT with LVGL 6 and hardcoded pixel
 * positions throughout. That does not survive the move to a 502x410 panel, so
 * this is rebuilt with LVGL 9 flex layout and percentage sizing: the same source
 * lays out correctly on both watches without per-board coordinates.
 *
 * Shows time, date, battery and charge state, all driven from the RTC and PMU
 * rather than millis(), so it stays correct across sleep.
 */

#include "simple_face.h"

#include "../usable_area/usable_area.h"

#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#else
#include <stdlib.h>
#include <time.h>
#endif

static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *label_batt;
static lv_obj_t *bar_batt;

static const char *weekday_name(uint8_t w)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[w > 6 ? 0 : w];
}

static void build_face(lv_obj_t *screen)
{
    // usable_area_style_screen() already painted/clipped the screen itself;
    // build the face inside the largest rect that's safe everywhere under
    // the curved bezel rather than against the screen's raw bounds.
    lv_obj_t *scr = safe_area_rect(screen);

    // Column layout with the clock centred and the battery row pinned bottom.
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 8, 0);

    // Font size is the one thing that genuinely differs between a 240px and a
    // 502px wide panel, so choose it from the actual resolution.
    bool small = lv_display_get_horizontal_resolution(NULL) <= 400;

    label_time = lv_label_create(scr);
    lv_obj_set_style_text_color(label_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_time,
                               small ? &lv_font_montserrat_48 : &lv_font_montserrat_48, 0);
    lv_label_set_text(label_time, "--:--");
    /*48px is the largest bitmap font LVGL ships, so scale the label itself
      2x for bigger digits. Scaling is draw-time only -- flex still reserves
      just the label's unscaled box -- so give it matching margin above and
      below or the enlarged render crowds label_date beneath it. Pivots
      around the label's own center by default, which flex already
      horizontally centers, so no extra alignment is needed.*/
    lv_obj_set_style_transform_scale(label_time, 2 * LV_SCALE_NONE, 0);
    lv_obj_set_style_margin_top(label_time, 24, 0);
    lv_obj_set_style_margin_bottom(label_time, 24, 0);

    label_date = lv_label_create(scr);
    lv_obj_set_style_text_color(label_date, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_date,
                               small ? &lv_font_montserrat_14 : &lv_font_montserrat_20, 0);
    lv_label_set_text(label_date, "");

    bar_batt = lv_bar_create(scr);
    lv_obj_set_size(bar_batt, LV_PCT(50), small ? 12 : 20);
    lv_bar_set_range(bar_batt, 0, 100);

    label_batt = lv_label_create(scr);
    lv_obj_set_style_text_color(label_batt, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_batt,
                               small ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
    lv_label_set_text(label_batt, "");
}

static void refresh(lv_timer_t *t)
{
    LV_UNUSED(t);

#ifdef ARDUINO
    RTC_DateTime now = instance.rtc.getDateTime();
    uint8_t hour = now.getHour();
    uint8_t minute = now.getMinute();
    // getWeek() is the weekday (0-6); getDay() is the day of month -- see
    // SensorRTC.h's own RTC_DateTime::printDatetime() for the same pairing.
    uint8_t week = now.getWeek();
    uint16_t year = now.getYear();
    uint8_t month = now.getMonth();
    uint8_t day = now.getDay();

    int percent = instance.pmu.getBatteryPercent();
    if (percent < 0) percent = 0;
    bool charging = instance.pmu.isCharging();
#else
    // No RTC/PMU on the host -- the wall clock stands in for the one, a
    // fixed/jittered reading for the other. Same shape as
    // src/factory/hal_interface.cpp's native stub (30 + rand() % 71).
    time_t raw = time(NULL);
    struct tm *lt = localtime(&raw);
    uint8_t hour = lt->tm_hour;
    uint8_t minute = lt->tm_min;
    uint8_t week = lt->tm_wday;
    uint16_t year = lt->tm_year + 1900;
    uint8_t month = lt->tm_mon + 1;
    uint8_t day = lt->tm_mday;

    int percent = 30 + rand() % 71;
    bool charging = false;
#endif

    lv_label_set_text_fmt(label_time, "%02u:%02u", hour, minute);
    lv_label_set_text_fmt(label_date, "%s  %04u-%02u-%02u",
                          weekday_name(week), year, month, day);

    lv_bar_set_value(bar_batt, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_batt, "%d%%  %s", percent, charging ? "charging" : "");
}

void simple_face_init(lv_obj_t *screen)
{
    build_face(screen);

    // One timer at 1 Hz is enough -- the display only shows minutes.
    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);      // paint immediately rather than after 1s
}
