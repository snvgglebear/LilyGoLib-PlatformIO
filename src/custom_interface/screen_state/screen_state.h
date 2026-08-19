#pragma once
#include <lvgl.h>
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define HAS_WRIST_TILT_SENSOR
#endif
#ifdef HAS_WRIST_TILT_SENSOR
/*Raise-to-wake thresholds, in GRAVITY_VECTOR units. That sensor reports a
  unit-normalised "which way is down", so each component is a direction cosine
  in -1..+1 -- not m/s^2, and not degrees.

  Measured over 124 samples of worn use: raised-to-look sits at z +0.74..+0.98
  (mean +0.89), arm-down at z -0.69..+0.30 (mean +0.00), with only 8% of
  samples anywhere between. One axis separates the two states cleanly, so
  there is nothing to gain from also constraining x and y.

  Two thresholds rather than one because a single edge chatters while the wrist
  hovers near it; the gap is the hysteresis band.*/
#define WRIST_RAISED_Z    0.70f
#define WRIST_RELEASED_Z  0.40f

/*A deliberate raise holds the angle; an arm swing crosses it and keeps going.
  Generous on purpose -- tighten only if raises feel slow to register.*/
#define WRIST_SETTLE_MS   200
#endif


/*Call once from setup(), after instance.begin(): wires up the power button
  and (on boards with a BHI260AP) the wrist-tilt wake gesture.*/
void screen_state_init(void);

/*Call every loop() iteration: handles touch/power-button/wrist-tilt wake and
  the idle sleep timeout.*/
void manageSleepState(void);

/*Registered callback fires once per asleep->awake transition, from
  manageSleepState() -- whichever of touch/power-button/wrist-tilt caused it.
  Screen navigation lives outside this module, so this is the hook for a
  caller that wants waking the display to also reset which app/screen is
  showing (e.g. always return to the watch face). Call before the first
  manageSleepState().*/
typedef void (*ScreenWakeCallback)(void);
void screen_state_set_wake_cb(ScreenWakeCallback cb);