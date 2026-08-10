/**
 * @file      ChargingAnimation.ino
 * @license   MIT
 * @brief     Animated battery that reflects real charge state.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/ChargingAnimation.
 *
 * The original played a fixed frame sequence whenever USB was present. This
 * version reads the AXP2101 directly, so the animation is a view of hardware
 * state rather than a loop: the fill level tracks the real percentage, the
 * sweep only runs while the PMU reports charging, and pulling the cable stops
 * it immediately.
 *
 * The animated part is an LVGL animation on the bar value, which keeps the
 * sweep smooth without any per-frame work in loop().
 *
 * @see https://docs.lvgl.io/master/details/main-components/animation.html
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

static lv_obj_t *bar;
static lv_obj_t *label_pct;
static lv_obj_t *label_state;
static lv_anim_t charge_anim;
static bool animating = false;

/// LVGL animation callback: drive the bar's value.
static void set_bar_value(void *obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

/// Sweep the bar from the current charge level up to full, repeatedly.
static void start_charge_animation(int32_t from)
{
    if (animating) {
        return;
    }
    animating = true;

    lv_anim_init(&charge_anim);
    lv_anim_set_var(&charge_anim, bar);
    lv_anim_set_exec_cb(&charge_anim, set_bar_value);
    lv_anim_set_values(&charge_anim, from, 100);
    lv_anim_set_duration(&charge_anim, 1500);
    lv_anim_set_repeat_count(&charge_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&charge_anim);
}

static void stop_charge_animation()
{
    if (!animating) {
        return;
    }
    animating = false;
    lv_anim_delete(bar, set_bar_value);
}

static void refresh(lv_timer_t *t)
{
    LV_UNUSED(t);

    int percent = instance.pmu.getBatteryPercent();
    if (percent < 0) percent = 0;

    bool charging = instance.pmu.isCharging();
    bool usb      = instance.pmu.isVbusIn();

    if (charging) {
        // Sweep from the real level to full, so the animation still conveys how
        // charged the battery actually is.
        start_charge_animation(percent);
        lv_label_set_text(label_state, "Charging");
        lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    } else {
        stop_charge_animation();
        lv_bar_set_value(bar, percent, LV_ANIM_ON);
        lv_label_set_text(label_state, usb ? "USB connected (full)" : "On battery");
        lv_obj_set_style_bg_color(bar,
                                  percent < 20 ? lv_palette_main(LV_PALETTE_RED)
                                               : lv_palette_main(LV_PALETTE_BLUE),
                                  LV_PART_INDICATOR);
    }

    lv_label_set_text_fmt(label_pct, "%d%%", percent);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 12, 0);

    bool small = lv_display_get_horizontal_resolution(NULL) <= 320;

    label_pct = lv_label_create(scr);
    lv_obj_set_style_text_color(label_pct, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_pct,
                               small ? &lv_font_montserrat_24 : &lv_font_montserrat_48, 0);

    bar = lv_bar_create(scr);
    lv_obj_set_size(bar, LV_PCT(70), small ? 18 : 30);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 6, 0);

    label_state = lv_label_create(scr);
    lv_obj_set_style_text_color(label_state, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);

    Serial.println("Plug and unplug USB to see the state change");
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
