#include "screen_state.h"

#include "../app_config.h"

static bool screen_asleep = false;
static uint32_t last_activity_ms = 0;
static ScreenWakeCallback wake_cb = NULL;

/*Runtime, not a #define: the settings page changes this live. 0 means never.*/
static uint32_t sleep_timeout_ms = (uint32_t)APP_SCREEN_TIMEOUT_DEFAULT_S * 1000u;
static bool wrist_wake_enabled = APP_WRIST_WAKE_DEFAULT;

void screen_state_set_wake_cb(ScreenWakeCallback cb)
{
    wake_cb = cb;
}

void screen_state_set_timeout_ms(uint32_t ms)
{
    sleep_timeout_ms = ms;
    /*Restart the countdown rather than measuring the new timeout against an
      idle period the user spent under the old one -- otherwise shortening the
      timeout on the settings page can sleep the display mid-tap.*/
    noteActivity();
}

uint32_t screen_state_get_timeout_ms(void)
{
    return sleep_timeout_ms;
}

void screen_state_set_wrist_wake(bool enable)
{
    wrist_wake_enabled = enable;
}

bool screen_state_get_wrist_wake(void)
{
#ifdef HAS_WRIST_TILT_SENSOR
    return wrist_wake_enabled;
#else
    return false;
#endif
}

/*True when the idle timer has run out. Factored out because the ARDUINO and
  native branches below both need it, and both must skip -- not evaluate -- the
  comparison when the timeout is disabled.*/
static bool idleExpired(uint32_t now_ms)
{
    return sleep_timeout_ms != 0 && (now_ms - last_activity_ms) >= sleep_timeout_ms;
}

/*Resets the idle countdown. millis() on hardware, the host monotonic clock
  natively, so each branch below defines it -- forward declared here because
  screen_state_set_timeout_ms() above calls it.*/
static void noteActivity(void);

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
#include <bosch/BoschSensorDataHelper.hpp>   // SensorLib; hardware-only, see the wrist-tilt block below

/*Only the BHI260AP-equipped boards (T-Watch-Ultra, T-LoRa-Pager) can raise a
  wrist-tilt gesture - T-Watch-S3 uses a BMA423 instead, which has no
  equivalent virtual sensor on this SDK.*/
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define HAS_WRIST_TILT_SENSOR
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

/*The gravity vector is read straight out of bhy2's FIFO rather than through
  SensorXYZ, because SensorLib's BoschParseCallbackManager cannot be used
  safely here.

  Its dispatch bounds the scan with the callback's "size" *parameter* - the
  frame's payload length - instead of the same-named member holding the entry
  count (BoschParseCallbackManager.hpp:151, compiled because __GNUC__ < 10 on
  this toolchain). Its storage is raw malloc'd memory whose Entry constructors
  never run. A gravity frame is 7 bytes, so with fewer than 7 registered
  callbacks the scan walks off the end of the initialised entries and tests
  uninitialised heap: a garbage .cb that passes the NULL check, and a garbage
  .id with a 1-in-256 chance of matching, at which point it calls a junk
  function pointer. At 25 Hz that is a crash within seconds.

  Subscribing extra sensors to pad the array would only hide it. Registering
  directly in bhy2's parse table replaces SensorLib's parseData for this id, so
  the broken manager is never entered at all.*/
static float gravity_scale = 0.0f;
static volatile float gravity_z = 0.0f;
static volatile bool gravity_updated = false;

static void gravityFifoCb(const struct bhy2_fifo_parse_data_info *info, void *private_data)
{
    /*data_ptr already excludes the leading sensor-id byte, so a 7-byte event
      leaves the 6 bytes of int16 xyz.*/
    if (!info || info->data_size < 7) {
        return;
    }
    struct bhy2_data_xyz d;
    bhy2_parse_xyz(info->data_ptr, &d);
    gravity_z = d.z * gravity_scale;
    gravity_updated = true;
}

/*bhy2_register_fifo_parse_callback() only fills free slots and never replaces,
  while dispatch returns the first id match - and SensorBHI260AP::initImpl()
  has already claimed every available id for parseData. The slot has to be
  overwritten in place to take delivery.*/
static bool hookFifoId(uint8_t id, bhy2_fifo_parse_callback_t cb)
{
    bhy2_dev *dev = instance.sensor.getHandler();
    if (!dev) {
        return false;
    }
    for (uint8_t i = 0; i < BHY2_MAX_SIMUL_SENSORS; i++) {
        if (dev->table[i].sensor_id == id) {
            dev->table[i].callback = cb;
            dev->table[i].callback_ref = NULL;
            return true;
        }
    }
    return false;
}

/*RAISED is entered only via SETTLING: a deliberate raise holds the angle, an
  arm swing passes through it and leaves before the settle time elapses. The
  release threshold sits well below the raise threshold so that a wrist hovering
  near the boundary cannot chatter the display on and off.*/
enum WristState {
    WRIST_DOWN,
    WRIST_SETTLING,
    WRIST_RAISED,
};

static WristState wrist_state = WRIST_DOWN;
static uint32_t wrist_settle_started_ms = 0;

void ScreenTiltEvent(void)
{
    if (!gravity_updated) {
        return;
    }
    gravity_updated = false;

    float z = gravity_z;

    switch (wrist_state) {
    case WRIST_DOWN:
        if (z > WRIST_RAISED_Z) {
            wrist_state = WRIST_SETTLING;
            wrist_settle_started_ms = millis();
        }
        break;

    case WRIST_SETTLING:
        if (z <= WRIST_RAISED_Z) {
            wrist_state = WRIST_DOWN;   /*passed through without settling - not a raise*/
        } else if (millis() - wrist_settle_started_ms >= WRIST_SETTLE_MS) {
            wrist_state = WRIST_RAISED;
            wrist_tilt_detected = true; /*one-shot edge, consumed by manageSleepState()*/
        }
        break;

    case WRIST_RAISED:
        if (z < WRIST_RELEASED_Z) {
            wrist_state = WRIST_DOWN;
        }
        break;
    }
}

#endif /*HAS_WRIST_TILT_SENSOR*/

void screen_state_init(void)
{
    instance.onEvent(powerButtonEventCb, POWER_EVENT, NULL);

#ifdef HAS_WRIST_TILT_SENSOR
    /*enable() is what registers the result callback *and* configures the
      virtual sensor; without it GRAVITY_VECTOR never reports and
      hasUpdated() stays false forever, leaving the detector permanently idle.

      sample_rate is not a polling interval - for BHY2 virtual sensors 0 Hz
      means "disabled". 25 Hz is fast enough to catch a deliberate raise
      without the FIFO traffic of the 100 Hz used in the SDK examples.

      SensorLib reports failures via log_e(), which CORE_DEBUG_LEVEL=0
      silences in this project, so check the return and say so over Serial.*/
    gravity_scale = instance.sensor.getScaling(SensorBHI260AP::GRAVITY_VECTOR);

    /*configure() enables the virtual sensor; hookFifoId() takes delivery of it.
      Both must succeed or the detector silently never runs, and SensorLib
      reports its own failures through log_e(), which CORE_DEBUG_LEVEL=0
      silences in this project.

      sample_rate is not a polling interval - for BHY2 virtual sensors 0 Hz
      means "disabled". 25 Hz is fast enough for a deliberate raise without the
      FIFO traffic of the 100 Hz used in the SDK examples.*/
    if (!instance.sensor.configure(SensorBHI260AP::GRAVITY_VECTOR, /*sample_rate*/ 25.0f,
                                   /*report_latency_ms*/ 0) ||
        !hookFifoId(SensorBHI260AP::GRAVITY_VECTOR, gravityFifoCb)) {
        Serial.printf("[screen_state] GRAVITY_VECTOR setup failed - raise-to-wake disabled\n");
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
        /*One-shot: cleared here, re-armed by the next DOWN->RAISED transition.
          Leaving it latched would hold the idle timer open for as long as the
          wrist stayed up, so the screen could never time out while raised.
          Cleared even when the setting is off, for the same reason -- the
          detector keeps running, only the wake is suppressed.*/
        wrist_tilt_detected = false;

        /*Gate the whole arm, not just the wake: with raise-to-wake off, a
          raise must not hold an awake screen open either, or the idle timeout
          would still be unreachable while the wrist is up.*/
        if (wrist_wake_enabled) {
            if (screen_asleep) {
                instance.wakeupDisplay();
                wake();
            }
            last_activity_ms = millis();
        }
    }
#endif

    if (!screen_asleep && idleExpired(millis())) {
        instance.sleepDisplay();
        screen_asleep = true;
    }
}

static void noteActivity(void)
{
    last_activity_ms = millis();
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

    if (!screen_asleep && idleExpired(hostMillis())) {
        printf("[screen_state] sleep (idle timeout)\n");
        screen_asleep = true;
    }
}

static void noteActivity(void)
{
    last_activity_ms = hostMillis();
}

#endif // ARDUINO
