#include "screen_state.h"

#define SCREEN_SLEEP_TIMEOUT_MS  10000

static bool screen_asleep = false;
static uint32_t last_activity_ms = 0;
static ScreenWakeCallback wake_cb = NULL;

void screen_state_set_wake_cb(ScreenWakeCallback cb)
{
    wake_cb = cb;
}

/// Flips screen_asleep and fires wake_cb, but only on an actual
/// asleep->awake edge -- safe to call from anywhere that just turned the
/// display back on, without every call site re-deriving that edge itself.
static void wake(void)
{
    if (!screen_asleep) {
        return;
    }
    screen_asleep = false;
    if (wake_cb) {
        wake_cb();
    }
}

#ifdef ARDUINO

#include <LilyGoLib.h>

/*Only the BHI260AP-equipped boards (T-Watch-Ultra, T-LoRa-Pager) can raise a
  wrist-tilt gesture - T-Watch-S3 uses a BMA423 instead, which has no
  equivalent virtual sensor on this SDK.*/
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define HAS_WRIST_TILT_SENSOR
#endif

static bool power_button_clicked = false;

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
      exposing WRIST_TILT_GESTURE. SensorLib logs failures via log_e(), but
      CORE_DEBUG_LEVEL=0 in this project silences that, so check + report
      over Serial directly instead of trusting it silently worked.

      sample_rate is not a polling interval here - for BHY2 virtual sensors
      0 Hz means "disabled". A gesture/event sensor still needs a nonzero
      rate to turn reporting on at all; the exact value doesn't change how
      often the gesture itself can fire.*/
    bool registered = instance.sensor.onResultEvent(SensorBHI260AP::WRIST_TILT_GESTURE, wristTiltResultCb);
    bool configured = registered && instance.sensor.configure(SensorBHI260AP::WRIST_TILT_GESTURE, 1, 0);

    if (!configured) {
        Serial.println("[screen_state] wrist-tilt gesture unavailable in this BHI260AP firmware image - wrist wake disabled, falling back to touch/power button only");
        instance.sensor.getSensorInfo().printInfo(Serial);
    } else {
        Serial.println("[screen_state] wrist-tilt wake enabled");
    }
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
            wake();
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
            wake();
        }

        last_activity_ms = millis();
    }
#ifdef HAS_WRIST_TILT_SENSOR
    else if (wrist_tilt_detected) {
        wrist_tilt_detected = false;

        if (screen_asleep) {
            instance.wakeupDisplay();
            wake();
        }

        last_activity_ms = millis();
    }
#endif

    if (!screen_asleep && (millis() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {
        instance.sleepDisplay();
        screen_asleep = true;
    }
}

#else // !ARDUINO -- native/SDL2 emulator build
/*
 * LilyGoLib's library.json declares "frameworks": ["arduino"], so PlatformIO
 * never adds its include path for the native platform the emulator envs
 * build against - <LilyGoLib.h> can't be included here at all, guarded or
 * not. There's also no PMU (power button) or BHI260AP (wrist tilt) on the
 * host, so this stub only has touch + an idle timer to work with; it fakes
 * just enough to run the same manageSleepState() call from main.cpp/.ino
 * without every caller needing its own #ifdef ARDUINO.
 */

#include <stdio.h>
#include <time.h>

static uint32_t hostMillis()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/// True if any pointer indev (the SDL2 window's mouse, standing in for
/// touch) is currently pressed.
static bool hostTouched()
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            return true;
        }
    }
    return false;
}

void screen_state_init(void)
{
    printf("[screen_state] no PMU/BHI260AP on the host -- power-button and wrist-tilt wake are hardware-only; touch + idle timeout still work\n");
    last_activity_ms = hostMillis();
}

void manageSleepState(void)
{
    if (hostTouched()) {
        if (screen_asleep) {
            printf("[screen_state] wake (touch)\n");
            wake();
        }
        last_activity_ms = hostMillis();
    }

    if (!screen_asleep && (hostMillis() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {
        printf("[screen_state] sleep (idle timeout)\n");
        screen_asleep = true;
    }
}

#endif // ARDUINO
