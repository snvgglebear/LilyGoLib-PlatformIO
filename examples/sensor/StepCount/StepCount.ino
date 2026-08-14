/**
 * @file      StepCount.ino
 * @license   MIT
 * @brief     Pedometer / step counter.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/BMA423_StepCount.
 *
 * The two watches count steps with completely different silicon, so this sketch
 * has two implementations behind one behaviour:
 *
 *   T-Watch-S3     BMA423   -- a hardware pedometer feature you enable and poll
 *                              with getPedometerCounter().
 *   T-Watch-Ultra  BHI260AP -- a sensor hub, where "step counter" is a virtual
 *                              sensor you subscribe to at a sample rate. The
 *                              SensorStepCounter helper wraps that subscription.
 *
 * LilyGoLib has BMA423 accelerometer/feature examples and BHI260 6DoF/Euler
 * examples, but nothing that counts steps on either board.
 *
 * The counters are free-running in the sensor, not in this sketch -- they keep
 * accumulating across resets until explicitly cleared.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

static lv_obj_t *label1;

#if defined(ARDUINO_T_WATCH_S3)
/* ------------------------------------------------------------------ BMA423 */

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Starting pedometer...");

    // The pedometer runs on top of the accelerometer, so that has to be on first.
    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();

    // enablePedometer() switches on the BMA423's step-detection feature.
    if (!instance.sensor.enablePedometer()) {
        lv_label_set_text(label1, "Failed to enable pedometer");
        Serial.println("enablePedometer failed");
        while (1) {
            lv_task_handler(); delay(5);
        }
    }

    // Start from zero so the reading reflects this session.
    instance.sensor.resetPedometer();

    Serial.println("Pedometer enabled, walk with the watch on");
}

void loop()
{
    static uint32_t last = 0;

    if (millis() - last > 1000) {
        last = millis();

        uint32_t steps = instance.sensor.getPedometerCounter();

        lv_label_set_text_fmt(label1, "Steps\n%lu", (unsigned long)steps);
        Serial.printf("steps: %lu\n", (unsigned long)steps);
    }

    lv_task_handler();
    delay(5);
}

#elif defined(USING_BHI260_SENSOR)
/* ---------------------------------------------------------------- BHI260AP */

#include <bosch/BoschSensorDataHelper.hpp>

// The helper subscribes to the sensor hub's STEP_COUNTER virtual sensor and
// caches the latest value for us.
static SensorStepCounter stepCounter(instance.sensor);

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Starting pedometer...");

    if (!(instance.getDeviceProbe() & HW_BHI260AP_ONLINE)) {
        lv_label_set_text(label1, "Sensor is not online");
        while (1) {
            lv_task_handler(); delay(5);
        }
    }

    // Steps are events, not a waveform: a low rate is plenty and keeps the hub
    // from waking the CPU more than necessary. 0 latency = report immediately.
    float sample_rate = 5.0;
    uint32_t report_latency_ms = 0;
    stepCounter.enable(sample_rate, report_latency_ms);

    Serial.println("Step counter subscribed, walk with the watch on");
}

void loop()
{
    static uint32_t last = 0;

    // The hub pushes data in; this pumps its event queue.
    instance.loop();

    if (millis() - last > 1000) {
        last = millis();

        uint32_t steps = stepCounter.getStepCount();

        lv_label_set_text_fmt(label1, "Steps\n%lu", (unsigned long)steps);
        Serial.printf("steps: %lu\n", (unsigned long)steps);
    }

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
