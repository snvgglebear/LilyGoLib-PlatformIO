#ifdef ARDUINO
#include "usable_area/usable_area.h"
#include "screen_state/screen_state.h"
#include "gadgetbridge_ble/gb_app.h"
#include "gadgetbridge_ble/gb_link.h"
#include "gadgetbridge_ble/gb_ui.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
void setup() {
  Serial.begin(115200);

  instance.begin();
  beginLvglHelper(instance);   // wire LVGL to the display + touch, before anything below touches it
  instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);   // instance.begin() leaves backlight at 0

  // Initialize the usable area
  usable_area_init();
  screen_state_init();

  gb_platform::begin();
  gb_ui_begin();                        // screens first, so the app can refresh them
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
