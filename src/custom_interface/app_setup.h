#pragma once

/**
 * @file      app_setup.h
 * @license   MIT
 * @brief     The screen/gesture/Gadgetbridge bring-up shared by custom_interface.ino's
 *            setup()/loop() and main.cpp's native entry point -- same split
 *            as src/factory's setupGui(), declared here and defined once in
 *            app_setup.cpp so hardware and the emulator build run identical
 *            UI code.
 */

/// Builds both screens (watch face + Gadgetbridge), the quick-settings tray
/// and the BOOT button's navigation, and starts Gadgetbridge. Call once, after
/// the display + LVGL are up (beginLvglHelper() on hardware, the SDL window
/// natively).
void setupGui();

/// Pumps LVGL, sleep state, the BOOT button and Gadgetbridge. Call every
/// iteration of loop() (Arduino) or the native main loop, after ticking LVGL's
/// clock.
void loopGui();
