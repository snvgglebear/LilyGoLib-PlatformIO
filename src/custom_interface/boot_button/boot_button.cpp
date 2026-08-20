/**
 * @file      boot_button.cpp
 * @license   MIT
 * @brief     Debounced GPIO 0 / keyboard polling for the BOOT button.
 *            See boot_button.h.
 */
#include "boot_button.h"

#include <stdint.h>

namespace
{

/// Settling time for the contact. Long enough to swallow the bounce on a
/// tactile switch, short enough that a deliberate press is never missed at the
/// loop's polling rate.
constexpr uint32_t BOOT_DEBOUNCE_MS = 40;

BootButtonAction s_action = nullptr;

/// Debounced level, and the raw level that is currently being timed. Pressed
/// is `true` in both, so the active-low pin is inverted at the one place it is
/// read and nothing below has to remember the polarity.
bool s_stable_pressed = false;
bool s_candidate_pressed = false;
uint32_t s_candidate_since_ms = 0;

/// Board-specific: sample the pin (or the stand-in key), and read a
/// millisecond clock. Defined by each branch at the bottom of this file.
bool bootPinPressed();
uint32_t bootMillis();

} // namespace

void boot_button_set_action(BootButtonAction cb)
{
    s_action = cb;
}

void boot_button_poll(void)
{
    const bool raw = bootPinPressed();
    const uint32_t now = bootMillis();

    if (raw != s_candidate_pressed) {
        // Level changed: start (or restart) timing it. Anything that flips
        // back before BOOT_DEBOUNCE_MS is bounce and never reaches the code
        // below.
        s_candidate_pressed = raw;
        s_candidate_since_ms = now;
        return;
    }
    if (raw == s_stable_pressed || now - s_candidate_since_ms < BOOT_DEBOUNCE_MS) {
        return;     // nothing new, or not settled yet
    }

    s_stable_pressed = raw;

    // Act on press, not release: a watch button should answer under the thumb
    // rather than when it comes off.
    if (s_stable_pressed && s_action) {
        s_action();
    }
}

#ifdef ARDUINO

#include <Arduino.h>

namespace
{

/// GPIO 0. Named here rather than taken from pins_arduino.h because the
/// variant headers do not define the BOOT button -- see boot_button.h.
constexpr uint8_t BOOT_BUTTON_PIN = 0;

bool bootPinPressed()
{
    return digitalRead(BOOT_BUTTON_PIN) == LOW;   // active low
}

uint32_t bootMillis()
{
    return millis();
}

} // namespace

void boot_button_init(void)
{
    // INPUT_PULLUP, not INPUT: the button shorts the pin to ground and there
    // is no external pull-up on this net, so a bare input would float.
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
}

#else // !ARDUINO -- native/SDL2 emulator build

/*
 * No GPIO on the host, so the B key stands in for the button and the same
 * navigation can be exercised with `pio run -e emulator_watch_ultra -t exec`.
 *
 * SDL's own keyboard state rather than an LVGL indev: lv_sdl_keyboard_create()
 * feeds a keypad indev, which only delivers keys to whichever widget has focus
 * -- a hardware button is not addressed to a widget. The SDL window driver
 * already pumps the event queue on its LVGL timer, so this snapshot is current
 * without any pumping here.
 */
#include <SDL2/SDL.h>
#include <stdio.h>

namespace
{

bool bootPinPressed()
{
    int count = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&count);
    return keys && count > SDL_SCANCODE_B && keys[SDL_SCANCODE_B] != 0;
}

uint32_t bootMillis()
{
    return static_cast<uint32_t>(SDL_GetTicks());
}

} // namespace

void boot_button_init(void)
{
    printf("[boot_button] no GPIO on the host -- press B for the BOOT button\n");
}

#endif // ARDUINO
