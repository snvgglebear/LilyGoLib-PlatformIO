/**
 * @file      AnalogRead.ino
 * @license   MIT
 * @brief     Live scrolling chart of battery voltage.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/AnalogRead.
 *
 * The original plotted a raw ADC pin. Neither watch exposes a free analog pin --
 * they are sealed units -- so this plots the one analog quantity that is always
 * available and actually interesting: battery voltage from the AXP2101.
 *
 * The useful part to copy is the pattern, not the signal: an lv_chart with a
 * shifting series is how you show any streaming value (RSSI, temperature,
 * accelerometer magnitude) without redrawing the whole widget each sample.
 *
 * @see https://docs.lvgl.io/master/details/widgets/chart.html
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

#define SAMPLE_COUNT 60      ///< one minute of history at 1 Hz

static lv_obj_t *chart;
static lv_chart_series_t *series;
static lv_obj_t *label_value;

static void sample(lv_timer_t *t)
{
    LV_UNUSED(t);

    uint16_t mv = instance.pmu.getBattVoltage();

    // lv_chart_set_next_value shifts the series along and appends, which is
    // exactly the scrolling behaviour we want.
    lv_chart_set_next_value(chart, series, mv);

    lv_label_set_text_fmt(label_value, "%u mV  (%d%%)",
                          mv, instance.pmu.getBatteryPercent());
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 10, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Battery voltage");

    chart = lv_chart_create(scr);
    // Percentage sizing keeps this sane on both 240x240 and 502x410.
    lv_obj_set_size(chart, LV_PCT(85), LV_PCT(50));
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, SAMPLE_COUNT);

    // A Li-ion cell lives between roughly 3.0V and 4.2V; fixing the range makes
    // small changes visible instead of autoscaling into a flat line.
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 3000, 4300);

    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN),
                                 LV_CHART_AXIS_PRIMARY_Y);

    label_value = lv_label_create(scr);

    lv_timer_t *timer = lv_timer_create(sample, 1000, NULL);
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
