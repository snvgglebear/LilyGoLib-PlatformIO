/**
 * @file      testing.ino
 * @license   MIT
 * @brief     Arduino/ESP32 entry point for the testing sketch.
 *
 * Board bring-up plus the hardware-only extras (BHI260 orientation sensor,
 * PMU power-button event, serial logging). The UI and sleep handling are
 * shared with main.cpp through app_setup.h, so the same screens run on the
 * emulator.
 *
 * Still guarded to the Ultra, as the original sketch was -- the other boards
 * would compile app_setup.cpp but have no setup()/loop() to link against.
 */
#ifdef ARDUINO
#include "app_setup.h"

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
// in screen_state_init(), after instance.begin():
#include <bosch/BoschSensorDataHelper.hpp>

static SensorXYZ accel(SensorBHI260AP::ACCEL_PASSTHROUGH, instance.sensor);
static SensorOrientation orientation(instance.sensor);
static uint32_t previousOrientation = 0;

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

    /*The CST9217 touch chip's raw axes don't match the panel's mounted
      orientation on this unit, and LilyGoWatchUltra::initTouch() never
      corrects for it (no setSwapXY/setMirrorXY call), so every swipe comes
      in reversed. setMaxCoordinates() must come first: the mirror math in
      TouchDrvInterface::updateXY() is `_xMax - x` / `_yMax - y`, gated on
      _xMax/_yMax being nonzero - they default to 0 and nothing else in this
      codebase sets them, so setMirrorXY() alone would silently no-op.*/
    //instance.touch.setMaxCoordinates(410, 502);
    //instance.touch.setMirrorXY(true, true);

    beginLvglHelper(instance);
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    setupGui();   // screens + sleep timer -- see app_setup.cpp
    // in screen_state_init(), after instance.begin():
    accel.enable(/*sample_rate*/ 25.0f, /*report_latency_ms*/ 0);
    previousOrientation = 0;
    instance.onEvent([](DeviceEvent_t event, void *params, void * user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            testing_on_power_button();
        }
    }, POWER_EVENT, NULL);

    float sample_rate = 5.0;
    uint32_t report_latency_ms = 0;
    orientation.enable(sample_rate, report_latency_ms);
}

void loop()
{
    instance.loop();

    /*Orientation polling is hardware-only -- there is no BHI260AP on the
      host, so this stays out of the shared loopGui().*/
      // resting x: +93ish,  +20ish, z +23ish
      // looking: x: +11, y: -78, z: +67

    static uint32_t last = 0;
    if (accel.hasUpdated()) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 200) {
        Serial.printf("accel x:%+6.2f y:%+6.2f z:%+6.2f\n",
                       accel.getX(), accel.getY(), accel.getZ());
        last_log = millis();
    }
}
    // if (millis() - last > 500) {
    //     last = millis();
    //     uint32_t o = orientation.getOrientation();
    //     Serial.printf("orientation: %lu (%s)\n", (unsigned long)o, orientation_name(o));

    //     if (previousOrientation != o) {
    //         Serial.printf("previous orientation: %lu (%s)\n",
    //                       (unsigned long)previousOrientation, orientation_name(previousOrientation));
    //     }

    //     previousOrientation = o;
    // }

    loopGui();
    delay(5);
}

#endif // ARDUINO_T_WATCH_S3_ULTRA
#endif // ARDUINO
