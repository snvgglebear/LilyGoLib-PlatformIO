#pragma once

/**
 * @file      boot_button.h
 * @license   MIT
 * @brief     The BOOT button as an app input.
 *
 * The T-Watch-Ultra's BOOT button is wired to GPIO 0 -- the ESP32-S3 strapping
 * pin, active low with the internal pull-up, held down at reset to enter
 * download mode. LilyGoLib knows the pin (LilyGoWatchUltra.cpp's
 * checkWakeupPins() arms `_BV(0)` for WAKEUP_SRC_BOOT_BUTTON) but only as a
 * deep-sleep wake source; there is no runtime event for it the way
 * instance.onEvent(POWER_EVENT) covers the power button. So this module polls
 * it.
 *
 * Polling, not an interrupt: the app already runs a loop at ~200 Hz and a
 * button that reports one loop late is imperceptible, whereas an ISR here
 * would have to hand the press across to LVGL's thread anyway.
 *
 * The press is delivered as a callback rather than acted on here, for the same
 * reason quick_settings_tray takes a QstAction: this module knows about one
 * GPIO and nothing about screens, and app_setup.cpp owns navigation.
 */

/// What a completed press runs. Called from boot_button_poll(), i.e. on the
/// same thread as LVGL, so it is safe to load screens from it.
typedef void (*BootButtonAction)(void);

/// Configure the pin. Call once from setupGui(). Natively this instead reports
/// which key stands in for the button (see boot_button.cpp).
void boot_button_init(void);

/// Set the callback, replacing any previous one. NULL disables the button.
void boot_button_set_action(BootButtonAction cb);

/// Sample the pin and run the action on a completed press. Call every
/// iteration of loopGui().
void boot_button_poll(void);
