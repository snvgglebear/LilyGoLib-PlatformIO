/**
 * @file      ui_boot_button.cpp
 * @license   MIT
 * @brief     BOOT button (GPIO0) as a runtime input.
 *
 * Polled once per loop() tick rather than interrupt-driven -- GPIO0 has no
 * debounce hardware, so an ISR would still need software debounce on top;
 * polling is simpler and matches the rest of this codebase's input handling
 * (touch, the power button, wrist-tilt are all polled from loop() already).
 *
 * Hardware reads the real pin. The emulator has no GPIO0, so it substitutes
 * SDL's raw keyboard-state array (SDL_GetKeyboardState()) for the 'B' key --
 * deliberately NOT lv_sdl_keyboard_create()'s indev: that driver only
 * forwards a fixed whitelist of control keys (arrows/enter/esc/tab/
 * backspace/delete/home/end/page up-down) to LVGL and silently drops
 * everything else, including letter keys, before any indev event fires, so a
 * "B" keydown would never reach an lv_indev callback at all. SDL's keyboard
 * state array is a separate, always-current snapshot that doesn't consume
 * the SDL event queue, so reading it here does not race lv_sdl_window's
 * internal SDL_PollEvent() drain (lv_sdl_window.c's sdl_event_handler,
 * driven by its own LVGL timer) the way adding a second SDL_PollEvent() loop
 * would.
 *
 * @see plans/boot-button-input-plan.md
 */
#include "ui_boot_button.h"
#include "ui_define.h"
#include "app_config.h"
#include "ui_notification_popup.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <SDL2/SDL.h>
#endif

enum BootButtonState { BB_IDLE, BB_DEBOUNCE, BB_PRESSED };
static BootButtonState s_state = BB_IDLE;
static uint32_t s_state_entered_ms = 0;

static bool s_torch_active = false;
static uint8_t s_saved_brightness = 0;
static lv_obj_t *s_torch_overlay = NULL;   // full-white lv_obj_t on lv_layer_top()

static bool boot_button_raw_pressed(void)
{
#ifdef ARDUINO
    return digitalRead(0) == LOW;   // active-low, INPUT_PULLUP
#else
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    return keys[SDL_SCANCODE_B] != 0;
#endif
}

void ui_boot_button_init(void)
{
#ifdef ARDUINO
    pinMode(0, INPUT_PULLUP);
#endif
}

/**
 * Short press: dismiss a showing toast, else go home unless already there.
 * Toast-dismiss takes priority -- a toast is drawn on lv_layer_top() above
 * whatever screen is open, so it's the thing actually in front of the user's
 * attention regardless of which tile is active.
 */
static void boot_button_on_short_press(void)
{
    if (ui_notification_popup_is_showing()) {
        ui_notification_popup_dismiss();
    } else if (!isinMenu()) {
        menu_show();
    }
    // else: already home, no toast up -- deliberately no-op rather than
    // replaying menu_show()'s transition/haptic for no visible change.
}

/**
 * Long press: toggle a full-screen white overlay + max backlight, i.e. a
 * torch. Toggle rather than hold-to-light: the "held" duration is only known
 * at release time in this state machine, so hold-to-light could only turn
 * the screen on *after* release, defeating the purpose.
 *
 * Blocks the idle-sleep timer while active via set_low_power_mode_flag()
 * (the same exported hook apps use to keep the screen awake while open,
 * e.g. audio playback) rather than reaching into ui_main.cpp's private
 * disp_timer directly -- there is no accessor for that timer, and this
 * accomplishes the identical "don't blank while in use" effect through the
 * mechanism ui_main.cpp already built for exactly this purpose.
 */
static void boot_button_on_long_press(void)
{
    if (!s_torch_active) {
        s_saved_brightness = hw_get_disp_backlight();
        hw_set_disp_backlight(255);

        s_torch_overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_torch_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(s_torch_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_torch_overlay, THEME_COLOR_WHITE, 0);
        lv_obj_set_style_bg_opa(s_torch_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_torch_overlay, 0, 0);
        lv_obj_set_style_radius(s_torch_overlay, 0, 0);
        lv_obj_remove_flag(s_torch_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_torch_overlay, LV_OBJ_FLAG_SCROLLABLE);

        set_low_power_mode_flag(false);
        s_torch_active = true;
    } else {
        hw_set_disp_backlight(s_saved_brightness);
        lv_obj_del(s_torch_overlay);
        s_torch_overlay = NULL;

        set_low_power_mode_flag(true);
        lv_disp_trig_activity(NULL);   // idle countdown restarts fresh from now
        s_torch_active = false;
    }
}

void ui_boot_button_poll(void)
{
    bool pressed = boot_button_raw_pressed();
    uint32_t now = lv_tick_get();

    switch (s_state) {
    case BB_IDLE:
        if (pressed) {
            s_state = BB_DEBOUNCE;
            s_state_entered_ms = now;
        }
        break;

    case BB_DEBOUNCE:
        if (!pressed) {
            s_state = BB_IDLE;   // bounce, ignore
        } else if (now - s_state_entered_ms >= BOOT_BUTTON_DEBOUNCE_MS) {
            s_state = BB_PRESSED;
            s_state_entered_ms = now;   // press-confirmed time
        }
        break;

    case BB_PRESSED:
        if (!pressed) {
            uint32_t held_ms = now - s_state_entered_ms;
            if (held_ms >= BOOT_BUTTON_LONG_PRESS_MS) {
                boot_button_on_long_press();
            } else {
                boot_button_on_short_press();
            }
            s_state = BB_IDLE;
        }
        break;
    }
}
