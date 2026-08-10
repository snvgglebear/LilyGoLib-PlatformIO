/**
 * @file      DRV2605_Realtime.ino
 * @license   MIT
 * @brief     Drive the haptic motor directly, bypassing the effect library.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/TwatcV2Special/DRV2605_Realtime.
 *
 * In real-time playback (RTP) mode the DRV2605 stops interpreting effect IDs and
 * simply drives the motor at whatever amplitude you write. That makes it the
 * mode to use when the vibration should track something continuous -- incoming
 * signal strength, a scrub gesture, a countdown -- rather than replay a canned
 * pattern.
 *
 * The table below is played as (amplitude, duration_ms) pairs: a rising ramp,
 * then three discrete pulses, then a pause before repeating.
 *
 * @see https://www.ti.com/lit/ds/symlink/drv2605.pdf  section 7.6.2
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

static lv_obj_t *label1;

/// (drive amplitude, hold time in ms) pairs. Amplitude 0x00 is "motor off".
static const uint8_t rtp_sequence[][2] = {
    {0x30, 100}, {0x32, 100}, {0x34, 100}, {0x36, 100},
    {0x38, 100}, {0x3A, 100},
    {0x00, 100},
    {0x40, 200}, {0x00, 100},
    {0x40, 200}, {0x00, 100},
    {0x40, 200}, {0x00, 100},
};
static const size_t rtp_steps = sizeof(rtp_sequence) / sizeof(rtp_sequence[0]);

/// Keep LVGL responsive while the current amplitude is held.
static void hold_ms(uint32_t ms)
{
    uint32_t until = millis() + ms;
    while (millis() < until) {
        lv_task_handler();
        delay(5);
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "DRV2605 realtime");

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Real-time playback: setRealtimeValue() now controls the motor directly.
    instance.drv.setMode(SensorDRV2605::MODE_REALTIME);

    Serial.println("DRV2605 in realtime mode");
}

void loop()
{
    for (size_t i = 0; i < rtp_steps; i++) {
        uint8_t amplitude = rtp_sequence[i][0];
        uint8_t duration  = rtp_sequence[i][1];

        instance.drv.setRealtimeValue(amplitude);
        lv_label_set_text_fmt(label1, "DRV2605 realtime\nstep %u/%u\namplitude 0x%02X",
                              (unsigned)(i + 1), (unsigned)rtp_steps, amplitude);
        hold_ms(duration);
    }

    // Always leave the motor stopped between passes, otherwise it keeps buzzing
    // at the last amplitude written.
    instance.drv.setRealtimeValue(0x00);
    lv_label_set_text(label1, "DRV2605 realtime\nidle");
    hold_ms(1000);
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
