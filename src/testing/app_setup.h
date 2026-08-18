#pragma once
#include <lvgl.h>

/**
 * @file      app_setup.h
 * @license   MIT
 * @brief     The parts of the testing sketch that are not entry-point or
 *            board specific, so hardware and the emulator run identical UI
 *            code -- the same split src/factory and src/custom_interface use.
 *
 * testing.ino (Arduino) and main.cpp (native/SDL2) both bring their platform
 * up and then call these two.
 */

/// Builds the UI on the active screen. Call once, after LVGL has a display:
/// beginLvglHelper() on hardware, lv_sdl_window_create() natively.
void setupGui(void);

/// Call every loop iteration: sleep/wake handling plus lv_task_handler().
void loopGui(void);

/// Hook for the Arduino PMU power-button event. Sleep state lives in
/// app_setup.cpp so both entry points share one implementation; there is no
/// PMU on the host, so nothing calls this natively.
void testing_on_power_button(void);
