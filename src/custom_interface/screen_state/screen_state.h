#pragma once
#include <lvgl.h>

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