/**
 * @file      DRV2605_Complex.ino
 * @license   MIT
 * @brief     Chain several DRV2605 waveforms into one haptic "sentence".
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/TwatcV2Special/DRV2605_Complex.
 * The original drove an Adafruit_DRV2605 through `ttgo->drv`; here the driver is
 * SensorLib's SensorDRV2605, reachable as `instance.drv`.
 *
 * LilyGoLib's own peripheral/Vibrate_Basic only fires a single canned effect via
 * setHapticEffects(). This example goes a level lower: the DRV2605 has eight
 * waveform slots that it plays back-to-back on one trigger, so you can build a
 * compound buzz (ramp up, then a sharp click) instead of one flat pulse.
 *
 * Effect IDs come from the DRV2605 datasheet section 11.2 (the "Waveform Library
 * Effects List"). Slot 0 is played first; writing 0 to a slot ends the sequence.
 *
 * @see https://www.ti.com/lit/ds/symlink/drv2605.pdf
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

static lv_obj_t *label1;
static uint32_t play_count = 0;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "DRV2605 complex effect");

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Internal trigger: the sequence plays when run() is called, rather than
    // being driven by the chip's external IN pin.
    instance.drv.setMode(SensorDRV2605::MODE_INTTRIG);

    // Library 1 is tuned for ERM motors, which is what the watch has fitted.
    instance.drv.selectLibrary(1);

    // Slot 0 then slot 1 play as a single gesture: a medium ramp that resolves
    // into a strong click. Slot 2 = 0 terminates the sequence.
    instance.drv.setWaveform(0, 84);    // effect 84: ramp up medium 1
    instance.drv.setWaveform(1, 1);     // effect  1: strong click, 100%
    instance.drv.setWaveform(2, 0);     // end of waveform list

    Serial.println("DRV2605 configured, playing sequence once per second");
}

void loop()
{
    instance.drv.run();

    lv_label_set_text_fmt(label1, "DRV2605 complex effect\nplayed %lu", ++play_count);
    Serial.printf("play %lu\n", play_count);

    // Give the sequence time to finish before re-triggering it.
    uint32_t until = millis() + 1000;
    while (millis() < until) {
        lv_task_handler();
        delay(5);
    }
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
