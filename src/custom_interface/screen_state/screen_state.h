#pragma once
#include <lvgl.h>

/*Call once from setup(), after instance.begin(): wires up the power button
  and (on boards with a BHI260AP) the wrist-tilt wake gesture.*/
void screen_state_init(void);

/*Call every loop() iteration: handles touch/power-button/wrist-tilt wake and
  the idle sleep timeout.*/
void manageSleepState(void);