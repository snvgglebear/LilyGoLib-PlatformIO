#include "screen_state.h"
#include <LilyGoLib.h>

#define SCREEN_SLEEP_TIMEOUT_MS  10000

/*Only the BHI260AP-equipped boards (T-Watch-Ultra, T-LoRa-Pager) can raise a
  wrist-tilt gesture - T-Watch-S3 uses a BMA423 instead, which has no
  equivalent virtual sensor on this SDK.*/
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define HAS_WRIST_TILT_SENSOR
#endif

static bool screen_asleep = false;
static bool power_button_clicked = false;
static uint32_t last_activity_ms = 0;

#ifdef HAS_WRIST_TILT_SENSOR
static bool wrist_tilt_detected = false;

static void wristTiltResultCb(uint8_t sensor_id, uint8_t *data, uint32_t size, uint64_t *timestamp, void *user_data)
{
    wrist_tilt_detected = true;
}
#endif

static void powerButtonEventCb(DeviceEvent_t event, void *params, void *user_data)
{
    if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
        power_button_clicked = true;
    }
}

void screen_state_init(void)
{
    instance.onEvent(powerButtonEventCb, POWER_EVENT, NULL);

#ifdef HAS_WRIST_TILT_SENSOR
    /*Whether this actually fires depends on the Bosch fusion firmware blob
      LilyGoUltra::initSensor() flashed (BOSCH_BHI260_KLIO / BOSCH_BHI260_GPIO)
      exposing WRIST_TILT_GESTURE - confirm on real hardware before relying on
      it as the only wake path.*/
    instance.sensor.onResultEvent(SensorBHI260AP::WRIST_TILT_GESTURE, wristTiltResultCb);
    instance.sensor.configure(SensorBHI260AP::WRIST_TILT_GESTURE, 0, 0);
#endif

    last_activity_ms = millis();
}

void manageSleepState(void)
{
    bool touched = instance.getTouched();

    if (power_button_clicked) {
        power_button_clicked = false;

        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;
        } else {
            instance.sleepDisplay();
            screen_asleep = true;
        }

        last_activity_ms = millis();
    } else if (touched) {
        /*Touch only wakes a sleeping screen / resets the idle timer - it must
          never put an already-awake screen to sleep, since a normal tap stays
          "touched" across many loop() iterations and would otherwise spam
          sleepDisplay()/wakeupDisplay() for the whole gesture.*/
        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;
        }

        last_activity_ms = millis();
    }
#ifdef HAS_WRIST_TILT_SENSOR
    else if (wrist_tilt_detected) {
        wrist_tilt_detected = false;

        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;
        }

        last_activity_ms = millis();
    }
#endif

    if (!screen_asleep && (millis() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {
        instance.sleepDisplay();
        screen_asleep = true;
    }
}
