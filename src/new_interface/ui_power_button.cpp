/**
 * @file      ui_power_button.cpp
 * @license   MIT
 * @brief     PMU side (power) button: single click toggles the display,
 *            a double click opens the power menu.
 *
 * The PMU only reports PMU_EVENT_KEY_CLICKED / PMU_EVENT_KEY_LONG_PRESSED --
 * there is no native double-click event to build on (see
 * hal_interface.h's hw_set_power_button_callback()), so a click is not acted
 * on immediately. Instead it starts a short (POWER_BUTTON_DOUBLE_PRESS_WINDOW_MS,
 * app_config.h) deferred timer; if a second click arrives before that timer
 * fires, the pair is treated as a double click (opens the power menu)
 * instead of two single-click toggles. This mirrors src/custom_interface's
 * screen_state.cpp reference for the click-toggles-the-display behaviour,
 * extended with the double-click case src/custom_interface/plan.md's
 * interface_bugfixes section adds on top of it.
 *
 * PMU_EVENT_KEY_LONG_PRESSED is ignored here -- the PMU already forces a
 * hardware power-off on its own long-press threshold, independent of this
 * software hook, and nothing in the bugfix list asks for a software action
 * on long press.
 */
#include "ui_power_button.h"
#include "ui_define.h"
#include "app_config.h"

static lv_timer_t *s_pending_timer = NULL;
static bool s_display_asleep = false;

static void perform_single_click_action(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_pending_timer = NULL;

    if (s_display_asleep) {
        hw_wakeup_display();
        s_display_asleep = false;
        lv_disp_trig_activity(NULL);   // idle countdown restarts fresh from now
    } else {
        hw_sleep_display();
        s_display_asleep = true;
    }
}

static void perform_double_click_action(void)
{
    if (s_display_asleep) {
        hw_wakeup_display();
        s_display_asleep = false;
    }
    extern app_t ui_power_main;
    open_app(&ui_power_main);
}

static void power_button_event_cb(bool long_press)
{
    if (long_press) {
        return;
    }

    if (s_pending_timer) {
        // Second click within the window -- cancel the deferred single-click
        // toggle and treat the pair as a double click instead.
        lv_timer_del(s_pending_timer);
        s_pending_timer = NULL;
        perform_double_click_action();
    } else {
        s_pending_timer = lv_timer_create(perform_single_click_action,
                                          POWER_BUTTON_DOUBLE_PRESS_WINDOW_MS, NULL);
        lv_timer_set_repeat_count(s_pending_timer, 1);
    }
}

void ui_power_button_init(void)
{
    hw_set_power_button_callback(power_button_event_cb);
}
