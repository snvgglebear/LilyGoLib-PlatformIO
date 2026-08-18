#include "screen_state.h"
#include <bosch/BoschSensorDataHelper.hpp>

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
static uint32_t last = 0;
#endif

static bool power_button_clicked = false;

static bool wrist_tilt_detected = false;

static void powerButtonEventCb(DeviceEvent_t event, void *params, void *user_data)
{
    if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
        power_button_clicked = true;
    }
}
#ifdef HAS_WRIST_TILT_SENSOR

/*Constructed on first call rather than at file scope on purpose:
  BoschSensorDataHelperBase's constructor caches getScaling() off the sensor,
  so building this before instance.begin() has booted the BHI260AP firmware
  would latch a bogus scale factor and make getX/getY/getZ read 0.0 forever.
  First call comes from screen_state_init(), which runs after begin().*/
static SensorXYZ &accelSensor(void)
{
    static SensorXYZ accel(SensorBHI260AP::ACCEL_PASSTHROUGH, instance.sensor);
    return accel;
}

void ScreenTiltEvent(void) {
    SensorXYZ &accel = accelSensor();
        if (accel.hasUpdated()) {
            if (abs(accel.getX() + LOOKING_X) <15 && abs(accel.getY() + LOOKING_Y) <15 && abs(accel.getZ() + LOOKING_Z<15)) {
                if (millis() - last > 200) {
                    Serial.printf("wrist tilt detected\n");
                    wrist_tilt_detected = true;
                    last = millis();
                }
            } else if (millis() - last > 1000) {
                Serial.printf("wrist tilt NOT detected x:%+6.2f y:%+6.2f z:%+6.2f\n",
                                accel.getX(), accel.getY(), accel.getZ());
                wrist_tilt_detected = false;
                last = millis();
            }
    }
}

#endif /*HAS_WRIST_TILT_SENSOR*/

void screen_state_init(void)
{
    instance.onEvent(powerButtonEventCb, POWER_EVENT, NULL);

#ifdef HAS_WRIST_TILT_SENSOR
    /*enable() is what registers the result callback *and* configures the
      virtual sensor; without it ACCEL_PASSTHROUGH never reports, so
      hasUpdated() stays false forever and ScreenTiltEvent() prints nothing.

      sample_rate is not a polling interval - for BHY2 virtual sensors 0 Hz
      means "disabled". 25 Hz per the plan: fast enough for a deliberate raise
      without the FIFO traffic of the 100 Hz used in the SDK examples.

      SensorLib reports failures via log_e(), which CORE_DEBUG_LEVEL=0
      silences in this project, so check the return and say so over Serial.*/
    if (!accelSensor().enable(/*sample_rate*/ 25.0f, /*report_latency_ms*/ 0)) {
        Serial.printf("[screen_state] ACCEL_PASSTHROUGH enable failed - wrist tilt disabled\n");
    }

#endif

    last_activity_ms = millis();
}

void manageSleepState(void)
{
    bool touched = instance.getTouched();
#ifdef HAS_WRIST_TILT_SENSOR
    ScreenTiltEvent();
#endif
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
        instance.wakeupDisplay();

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
