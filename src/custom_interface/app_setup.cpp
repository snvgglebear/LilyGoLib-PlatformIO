/**
 * @file      app_setup.cpp
 * @license   MIT
 * @brief     Screen/gesture/Gadgetbridge bring-up. See app_setup.h.
 *
 * Ported out of custom_interface.ino unchanged except for the ARDUINO/native log line,
 * so the same setupGui()/loopGui() build and drive the app on hardware
 * (twatch_ultra) and in the emulator (emulator_watch_ultra -t exec).
 */
#include "app_setup.h"

#include <lvgl.h>

#include <usable_area.h>
#include "app_config.h"
#include "boot_button/boot_button.h"
#include "screen_state/screen_state.h"
#include "gadgetbridge_ble/gb_app.h"
#include "gadgetbridge_ble/gb_link.h"
#include "gadgetbridge_ble/gb_ui.h"
#include "settings/app_settings.h"
#include "settings/settings_screen.h"
#include "watch_faces/face_registry.h"
#include "quick_settings_tray/quick_settings_tray.h"
#include "wifi/wifi_hal.h"
#include "wifi/wifi_service.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdio.h>
#endif

namespace
{

/*Two lv_screen_load()-able screens: the watch face is the default/home
  screen, the Gadgetbridge UI lives on its own screen and is only reached by
  swiping down from home (and back up again from there). Kept as separate
  screens rather than stacked on one so each can use the full usable area
  without the other's widgets sharing its flex layout.*/
lv_obj_t *screen_home;
lv_obj_t *screen_gadgetbridge;

/*Screen-slide duration and the touch thresholds now live in app_config.h, with
  the rest of the app's knobs -- see APP_SCREEN_ANIM_MS and the APP_TOUCH_*
  block. The value below is the old local constant's, unchanged.*/
constexpr uint32_t SCREEN_ANIM_MS = APP_SCREEN_ANIM_MS;

/**
 * Push the app's swipe/scroll thresholds into every pointer device.
 *
 * A fix-up rather than configuration: LVGL copies its own LV_INDEV_DEF_*
 * values into a device when the device is created, and there is no way to
 * change those defaults -- they are unguarded #defines in lv_indev.c that
 * lv_conf.h never sees. So the devices are created by beginLvglHelper() with
 * LVGL's numbers and corrected here.
 *
 * Loops rather than taking lv_indev_get_next(NULL) and stopping, because a
 * board can register more than one device: the T-LoRa-Pager adds an encoder
 * and a keyboard, and neither has gestures to tune.
 */
void applyTouchTuning()
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            continue;
        }
        lv_indev_set_gesture_min_distance(indev, APP_TOUCH_GESTURE_DISTANCE);
        lv_indev_set_gesture_min_velocity(indev, APP_TOUCH_GESTURE_VELOCITY);
        lv_indev_set_scroll_limit(indev, APP_TOUCH_SCROLL_LIMIT);
        lv_indev_set_scroll_throw(indev, APP_TOUCH_SCROLL_THROW);
    }
}

/**
 * True when the drag that produced this gesture is already scrolling
 * something, in which case the gesture is a side effect of that scroll and
 * not a screen navigation the user asked for.
 *
 * LVGL runs indev_gesture() on every LV_EVENT_PRESSING regardless of whether
 * a scroll is in progress, and LV_OBJ_FLAG_GESTURE_BUBBLE is set by default
 * on every object with a parent, so one finger drag both scrolls a list (or
 * swipes between tabs) *and* fires LV_EVENT_GESTURE at the screen. Scroll
 * ownership is settled by the time a gesture arrives -- LVGL claims a scroll
 * at 10px of travel (LV_INDEV_DEF_SCROLL_LIMIT) but only fires the gesture at
 * 50px (LV_INDEV_DEF_GESTURE_LIMIT) -- so this test is reliable.
 *
 * It is not a blanket mute: lv_indev_find_scroll_obj() only claims an object
 * that can actually scroll further in the drag's direction, so a list already
 * at its end claims nothing and the gesture goes through as before. That is
 * "scroll first, navigate when there is nothing left to scroll".
 */
bool gestureOwnedByScroll(lv_indev_t *indev)
{
    return lv_indev_get_scroll_obj(indev) != NULL;
}

/// Fires on any gesture that bubbles up to screen_home; only a downward
/// swipe (LV_DIR_BOTTOM: finger moves toward the bottom of the screen)
/// switches away from the watch face.
void onHomeGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || gestureOwnedByScroll(indev)) {
        return;   // nothing on the watch face scrolls today; guards the next thing that does
    }
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        gb_ui_show_home();   // always enter the Gadgetbridge screen on its launcher grid
        lv_screen_load_anim(screen_gadgetbridge, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, SCREEN_ANIM_MS, 0, false);
    }
}

/// Mirror of onHomeGesture() for the Gadgetbridge screen: an upward swipe
/// (LV_DIR_TOP) returns to the watch face. A downward swipe (LV_DIR_BOTTOM)
/// opens the quick-settings tray instead. Left/right are not handled here --
/// they belong to the tabview's page navigation, and gestureOwnedByScroll()
/// stops them reaching this handler at all -- deliberately not wired on
/// screen_home, whose own downward swipe already means "go to Gadgetbridge"
/// (see swipe-down-quick-settings-tray-plan.md's scope: the tray reads
/// battery/brightness, which the watch face already shows).
void onGadgetbridgeGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || gestureOwnedByScroll(indev)) {
        return;   // scrolling a list, or swiping between the tabview's pages
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP) {
        lv_screen_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_TOP, SCREEN_ANIM_MS, 0, false);
    } else if (dir == LV_DIR_BOTTOM) {
        quick_settings_tray_open();
    }
}

/**
 * The BOOT button (boot_button.h), as one "home" key:
 *
 *   anywhere but the launcher grid  ->  the launcher grid
 *   on the launcher grid            ->  back out to the watch face
 *
 * So a press always moves toward somewhere known, and two presses from any
 * screen in the app reach the watch face. The watch face itself is "anywhere
 * but the grid", so a press there opens the grid -- which makes the pair a
 * toggle once the user is already home, and the shortest path in from the
 * face.
 */
void onBootButton()
{
    // A dark screen means the press was aimed at waking the watch. Navigating
    // on it would move the user somewhere they never saw, and every other wake
    // source (touch, power button, wrist raise) also only wakes.
    if (screen_state_is_asleep()) {
        screen_state_wake_display();
        return;
    }

    // The tray floats on lv_layer_top() and would survive the screen load,
    // hanging over wherever we went -- the same trap quick_settings_tray's own
    // gear action documents. With it open the press means "put this away".
    if (quick_settings_tray_is_open()) {
        quick_settings_tray_close();
        return;
    }

    // Already home with nothing layered over it: the only place left to go is
    // out. Matches the upward swipe, animation included.
    if (lv_screen_active() == screen_gadgetbridge && gb_ui_at_home()) {
        lv_screen_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_TOP, SCREEN_ANIM_MS, 0, false);
        return;
    }

    /*Unconditional, and before the screen test: from a Gadgetbridge page this
      is the whole action (reset to the grid, closing any conversation view),
      and from another screen it makes sure the grid is what comes up rather
      than the page the user last left behind.*/
    gb_ui_show_home();
    if (lv_screen_active() != screen_gadgetbridge) {
        lv_screen_load_anim(screen_gadgetbridge, LV_SCR_LOAD_ANIM_MOVE_BOTTOM,
                            SCREEN_ANIM_MS, 0, false);
    }
}

/// screen_state's wake callback: waking the display always shows the watch
/// face again, regardless of which screen was active when it went dark --
/// an instant cut, not a swipe, since this isn't a gesture the user made.
void onScreenWake()
{
    lv_screen_load(screen_home);
}

} // namespace

void setupGui()
{
    /*First: the input devices already exist (beginLvglHelper() created them
      before setupGui() was called) and every gesture handler wired up below
      depends on how they are tuned.*/
    applyTouchTuning();

    // Initialize the usable area
    usable_area_init();          // styles/clips whichever screen is active now -- that's screen_home
    screen_state_init();
    boot_button_init();
    quick_settings_tray_init();  // lv_layer_top(), so it overlays every screen below -- needs screen_w/h from usable_area_init()

    /*Before the watch face: the store decides which face that is, and it also
      pushes the saved brightness and idle timeout into the subsystems set up
      just above.*/
    app_settings_begin();

    /*Split in two on purpose. wifi_hal_begin() only puts the radio in station
      mode and registers the got-IP/SNTP handlers -- cheap, and nothing is
      switched on. wifi_service_begin() then reads app_settings().wifi_enabled
      (loaded just above) plus its own saved credentials and decides whether to
      bring the radio up and start connecting, so it has to come after both.*/
    wifi_hal_begin();
    wifi_service_begin();

    screen_home = lv_screen_active();
    watch_face_begin(screen_home);   // builds the saved face; the settings page switches it later
    lv_obj_add_event_cb(screen_home, onHomeGesture, LV_EVENT_GESTURE, NULL);
    screen_state_set_wake_cb(onScreenWake);

    settings_screen_init();
    quick_settings_tray_set_action(settings_screen_open);   // the tray's gear
    boot_button_set_action(onBootButton);

    screen_gadgetbridge = lv_obj_create(NULL);
    usable_area_style_screen(screen_gadgetbridge);   // usable_area_init() only styled screen_home
    lv_obj_add_event_cb(screen_gadgetbridge, onGadgetbridgeGesture, LV_EVENT_GESTURE, NULL);

    gb_platform::begin();
    gb_ui_begin(screen_gadgetbridge);     // screens first, so the app can refresh them
    gb_app.begin(gb_ui_on_state_changed);
#ifdef ARDUINO
    Serial.printf("[gb] advertising as \"%s\"\n", gb_link_device_name());
#else
    printf("[gb] advertising as \"%s\"\n", gb_link_device_name());
#endif
}

void loopGui()
{
    lv_timer_handler();
    manageSleepState();
    /*After manageSleepState(), so a press that arrives on a sleeping screen is
      tested against the sleep state it actually landed on rather than one this
      same iteration has already cleared.*/
    boot_button_poll();
    gb_app.poll();
    /*Connect timeouts, scan completion and reconnects. Cheap when the radio is
      off, which is its default state.*/
    wifi_service_poll();
}
