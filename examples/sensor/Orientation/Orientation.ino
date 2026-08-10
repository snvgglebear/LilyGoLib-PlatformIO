/**
 * @file      Orientation.ino
 * @license   MIT
 * @brief     Report which way up the watch is being held.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/BMA423_Direction.
 *
 * This is the primitive behind auto-rotate and raise-to-wake. As with StepCount,
 * the two boards get there differently:
 *
 *   T-Watch-S3     BMA423   -- direction() returns one of six face/edge codes,
 *                              derived on-chip from the accelerometer.
 *   T-Watch-Ultra  BHI260AP -- DEVICE_ORIENTATION is a virtual sensor on the hub,
 *                              wrapped here by the SensorOrientation helper. It
 *                              reports Android-style portrait/landscape codes.
 *
 * The two encodings are not the same, so each branch maps its own values to text
 * rather than pretending there is one shared enum.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

static lv_obj_t *label1;

#if defined(ARDUINO_T_WATCH_S3)
/* ------------------------------------------------------------------ BMA423 */

static const char *direction_name(uint8_t dir)
{
    switch (dir) {
    case SensorBMA423::DIRECTION_BOTTOM_LEFT:  return "bottom left";
    case SensorBMA423::DIRECTION_TOP_RIGHT:    return "top right";
    case SensorBMA423::DIRECTION_TOP_LEFT:     return "top left";
    case SensorBMA423::DIRECTION_BOTTOM_RIGHT: return "bottom right";
    case SensorBMA423::DIRECTION_BOTTOM:       return "face down";
    case SensorBMA423::DIRECTION_TOP:          return "face up";
    default:                                   return "unknown";
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Tilt the watch");

    instance.sensor.configAccelerometer();
    instance.sensor.enableAccelerometer();

    Serial.println("Reading BMA423 direction");
}

void loop()
{
    static uint32_t last = 0;

    if (millis() - last > 500) {
        last = millis();

        uint8_t dir = instance.sensor.direction();

        lv_label_set_text_fmt(label1, "Orientation\n%s\n(code %u)", direction_name(dir), dir);
        Serial.printf("direction: %u (%s)\n", dir, direction_name(dir));
    }

    lv_task_handler();
    delay(5);
}

#elif defined(USING_BHI260_SENSOR)
/* ---------------------------------------------------------------- BHI260AP */

#include <bosch/BoschSensorDataHelper.hpp>

static SensorOrientation orientation(instance.sensor);

/// BHI260 device-orientation codes, per the BHY2 sensor API.
static const char *orientation_name(uint32_t o)
{
    switch (o) {
    case 0:  return "portrait upright";
    case 1:  return "landscape left";
    case 2:  return "portrait upside down";
    case 3:  return "landscape right";
    default: return "unknown";
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Tilt the watch");

    if (!(instance.getDeviceProbe() & HW_BHI260AP_ONLINE)) {
        lv_label_set_text(label1, "Sensor is not online");
        while (1) {
            lv_task_handler(); delay(5);
        }
    }

    float sample_rate = 5.0;
    uint32_t report_latency_ms = 0;
    orientation.enable(sample_rate, report_latency_ms);

    Serial.println("Subscribed to BHI260 device orientation");
}

void loop()
{
    static uint32_t last = 0;

    instance.loop();

    if (millis() - last > 500) {
        last = millis();

        uint32_t o = orientation.getOrientation();

        lv_label_set_text_fmt(label1, "Orientation\n%s\n(code %lu)",
                              orientation_name(o), (unsigned long)o);
        Serial.printf("orientation: %lu (%s)\n", (unsigned long)o, orientation_name(o));
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
