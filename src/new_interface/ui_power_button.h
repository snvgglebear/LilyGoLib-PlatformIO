/**
 * @file      ui_power_button.h
 * @license   MIT
 * @brief     PMU side (power) button: single click toggles the display,
 *            a double click opens the power menu.
 *
 * Hardware only (PMU_EVENT_* is delivered through instance.onEvent(), which
 * only exists under ARDUINO) -- ui_power_button_init() is a no-op on the
 * emulator.
 */
#pragma once

/// Register the PMU power-button callback (hal_interface.h's
/// hw_set_power_button_callback()). Call once from setupGui().
void ui_power_button_init(void);
