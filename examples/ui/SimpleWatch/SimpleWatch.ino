/**
 * @file      SimpleWatch.ino
 * @license   MIT
 * @brief     A minimal but complete watch face.
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

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *label_batt;
static lv_obj_t *bar_batt;

static const char *weekday_name(uint8_t w)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[w > 6 ? 0 : w];
}

static void build_face()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

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

    RTC_DateTime now = instance.rtc.getDateTime();

    lv_label_set_text_fmt(label_time, "%02u:%02u", now.getHour(), now.getMinute());
    lv_label_set_text_fmt(label_date, "%s  %04u-%02u-%02u",
                          weekday_name(now.getDay()), now.getYear(), now.getMonth(), now.getDay());

    int percent = instance.pmu.getBatteryPercent();
    if (percent < 0) percent = 0;

    lv_bar_set_value(bar_batt, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_batt, "%d%%  %s", percent,
                          instance.pmu.isCharging() ? "charging" : "");
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    build_face();

    // One timer at 1 Hz is enough -- the display only shows minutes.
    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);      // paint immediately rather than after 1s
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
