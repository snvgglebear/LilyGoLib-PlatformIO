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
#include "screen_state/screen_state.h"
#include "gadgetbridge_ble/gb_app.h"
#include "gadgetbridge_ble/gb_link.h"
#include "gadgetbridge_ble/gb_ui.h"
#include "settings/app_settings.h"
#include "settings/settings_screen.h"
#include "watch_faces/face_registry.h"
#include "quick_settings_tray/quick_settings_tray.h"

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
        lv_screen_load_anim(screen_gadgetbridge, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 220, 0, false);
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
        lv_screen_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
    } else if (dir == LV_DIR_BOTTOM) {
        quick_settings_tray_open();
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
    // Initialize the usable area
    usable_area_init();          // styles/clips whichever screen is active now -- that's screen_home
    screen_state_init();
    quick_settings_tray_init();  // lv_layer_top(), so it overlays every screen below -- needs screen_w/h from usable_area_init()

    /*Before the watch face: the store decides which face that is, and it also
      pushes the saved brightness and idle timeout into the subsystems set up
      just above.*/
    app_settings_begin();

    screen_home = lv_screen_active();
    watch_face_begin(screen_home);   // builds the saved face; the settings page switches it later
    lv_obj_add_event_cb(screen_home, onHomeGesture, LV_EVENT_GESTURE, NULL);
    screen_state_set_wake_cb(onScreenWake);

    settings_screen_init();
    quick_settings_tray_set_action(settings_screen_open);   // the tray's gear

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
    gb_app.poll();
}
