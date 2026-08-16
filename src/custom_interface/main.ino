#ifdef ARDUINO
#include "usable_area/usable_area.h"
#include "screen_state/screen_state.h"
#include "gadgetbridge_ble/gb_app.h"
#include "gadgetbridge_ble/gb_link.h"
#include "gadgetbridge_ble/gb_ui.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "watch_faces/simple_face.h"
#include "watch_faces/batman_dial.h"   // alternate analog face -- see setup()
#if defined(ARDUINO_T_WATCH_S3_ULTRA)

/*Two lv_screen_load()-able screens: the watch face is the default/home
  screen, the Gadgetbridge UI lives on its own screen and is only reached by
  swiping down from home (and back up again from there). Kept as separate
  screens rather than stacked on one so each can use the full usable area
  without the other's widgets sharing its flex layout.*/
static lv_obj_t *screen_home;
static lv_obj_t *screen_gadgetbridge;

/// Fires on any gesture that bubbles up to screen_home; only a downward
/// swipe (LV_DIR_BOTTOM: finger moves toward the bottom of the screen)
/// switches away from the watch face.
static void onHomeGesture(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
    lv_screen_load_anim(screen_gadgetbridge, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 220, 0, false);
  }
}

/// Mirror of onHomeGesture() for the Gadgetbridge screen: an upward swipe
/// (LV_DIR_TOP) returns to the watch face.
static void onGadgetbridgeGesture(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
    lv_screen_load_anim(screen_home, LV_SCR_LOAD_ANIM_MOVE_TOP, 220, 0, false);
  }
}

/// screen_state's wake callback: waking the display always shows the watch
/// face again, regardless of which screen was active when it went dark --
/// an instant cut, not a swipe, since this isn't a gesture the user made.
static void onScreenWake()
{
  lv_screen_load(screen_home);
}

void setup() {
  Serial.begin(115200);

  instance.begin();
  beginLvglHelper(instance);   // wire LVGL to the display + touch, before anything below touches it
  instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);   // instance.begin() leaves backlight at 0

  // Initialize the usable area
  usable_area_init();          // styles/clips whichever screen is active now -- that's screen_home
  screen_state_init();

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
  Serial.printf("[gb] advertising as \"%s\"\n", gb_link_device_name());
}
void loop() {
  instance.loop();
  lv_task_handler();
  manageSleepState();
  gb_app.poll();
}
#endif
#endif
