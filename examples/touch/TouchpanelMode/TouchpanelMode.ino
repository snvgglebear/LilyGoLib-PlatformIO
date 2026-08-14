/**
 * @file      TouchpanelMode.ino
 * @license   MIT
 * @brief     Sleep the display and wake it again on touch.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/TouchpanelMode.
 *
 * The original toggled the touch controller's power modes directly. LilyGoLib
 * does not expose the touch driver object, so the equivalent here is built from
 * what it does expose: sleepDisplay() / wakeupDisplay() for the panel, and
 * wakeupTouch() to bring the touch controller back out of its low-power state.
 *
 * This is the pattern a real watch face needs -- blank on idle to save battery,
 * come back on the first tap -- and it matters that wakeupTouch() is called on
 * resume, because on these boards the controller sleeps with the display and a
 * bare wakeupDisplay() leaves you with a lit but unresponsive screen.
 *
 * Tap the screen to sleep it; tap again to wake.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef USING_INPUT_DEV_TOUCHPAD

static lv_obj_t *label1;
static bool display_asleep = false;
static uint32_t sleep_at_ms = 0;

/// Idle time before the demo blanks the screen by itself.
#define AUTO_SLEEP_MS 10000

static void go_to_sleep()
{
    Serial.println("display -> sleep");
    display_asleep = true;
    instance.sleepDisplay();
}

static void wake_up()
{
    Serial.println("display -> wake");
    // Order matters: the touch controller has to be woken too, or the screen
    // lights up but never reports another press.
    instance.wakeupTouch();
    instance.wakeupDisplay();
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    display_asleep = false;
    sleep_at_ms = millis() + AUTO_SLEEP_MS;
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Tap to sleep the screen\nit also sleeps after 10s");

    sleep_at_ms = millis() + AUTO_SLEEP_MS;
}

void loop()
{
    bool touched = instance.getTouched();

    if (display_asleep) {
        // While asleep LVGL is not driving the panel; the only job is to watch
        // for the touch that brings it back.
        if (touched) {
            wake_up();
            // Wait for the finger to lift so this same press is not immediately
            // read as "sleep again".
            while (instance.getTouched()) {
                delay(10);
            }
        }
        delay(20);
        return;
    }

    if (touched) {
        while (instance.getTouched()) {
            lv_task_handler();
            delay(10);
        }
        go_to_sleep();
        return;
    }

    if (millis() > sleep_at_ms) {
        go_to_sleep();
        return;
    }

    lv_label_set_text_fmt(label1,
                          "Tap to sleep the screen\nauto sleep in %lus",
                          (unsigned long)((sleep_at_ms - millis()) / 1000));

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
    Serial.println("The example only support boards with a touch panel"); delay(1000);
}

#endif
