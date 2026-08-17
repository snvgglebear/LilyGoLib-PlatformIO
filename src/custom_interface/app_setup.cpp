/**
 * @file      app_setup.cpp
 * @license   MIT
 * @brief     Screen/gesture/Gadgetbridge bring-up. See app_setup.h.
 *
 * Ported out of main.ino unchanged except for the ARDUINO/native log line,
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
#include "watch_faces/simple_face.h"
#include "watch_faces/batman_dial.h"   // alternate analog face -- see setupGui()
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

/// Fires on any gesture that bubbles up to screen_home; only a downward
/// swipe (LV_DIR_BOTTOM: finger moves toward the bottom of the screen)
/// switches away from the watch face.
void onHomeGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        lv_screen_load_anim(screen_gadgetbridge, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 220, 0, false);
    }
}

/// Mirror of onHomeGesture() for the Gadgetbridge screen: an upward swipe
/// (LV_DIR_TOP) returns to the watch face. A downward swipe (LV_DIR_BOTTOM)
/// opens the quick-settings tray instead -- deliberately not wired on
/// screen_home, whose own downward swipe already means "go to Gadgetbridge"
/// (see swipe-down-quick-settings-tray-plan.md's scope: the tray reads
/// battery/brightness, which the watch face already shows).
void onGadgetbridgeGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
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

    screen_home = lv_screen_active();
    //simple_face_init(screen_home);
    batman_dial_init(screen_home);   // swap in for the analog face instead -- also comment out simple_face_init() above
    lv_obj_add_event_cb(screen_home, onHomeGesture, LV_EVENT_GESTURE, NULL);
    screen_state_set_wake_cb(onScreenWake);

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
