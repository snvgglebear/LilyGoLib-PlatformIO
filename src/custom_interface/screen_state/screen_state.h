#pragma once
#include <lvgl.h>
#if defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define HAS_WRIST_TILT_SENSOR
#endif
#ifdef HAS_WRIST_TILT_SENSOR
#define RESTING_X 90
#define RESTING_Y 20
#define RESTING_Z 23
#define LOOKING_X 10
#define LOOKING_Y -75
#define LOOKING_Z 65
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