/**
 * @file      ui_boot_button.h
 * @license   MIT
 * @brief     BOOT button (GPIO0) as a runtime input: short press = go home /
 *            dismiss the current notification toast, long press = flashlight.
 *
 * @see plans/boot-button-input-plan.md
 */
#pragma once

/// Configure GPIO0 for polling. Call once from setup() (new_interface.ino) or
/// main() (main.cpp), before the first ui_boot_button_poll() call. No-op on
/// the emulator -- SDL's key state needs no init.
void ui_boot_button_init(void);

/// Debounce/long-press state machine tick. Call once per loop() / hal_loop()
/// iteration. Cheap when idle (a single pin/key read).
void ui_boot_button_poll(void);
