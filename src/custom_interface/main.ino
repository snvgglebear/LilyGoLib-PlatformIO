#ifdef ARDUINO
#include "app_setup.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <lvgl.h>
#include "watch_faces/simple_face.h"
#include "watch_faces/batman_dial.h"   // alternate analog face -- see setup()
#if defined(ARDUINO_T_WATCH_S3_ULTRA)

void setup() {
  Serial.begin(115200);

  instance.begin();
  beginLvglHelper(instance);   // wire LVGL to the display + touch, before anything below touches it
  instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);   // instance.begin() leaves backlight at 0

  setupGui();   // screens, gestures, quick-settings tray, Gadgetbridge -- see app_setup.cpp
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
  loopGui();
}
#endif
#endif
