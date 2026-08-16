#ifdef ARDUINO
#include "app_setup.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#if defined(ARDUINO_T_WATCH_S3_ULTRA)

void setup() {
  Serial.begin(115200);

  instance.begin();
  beginLvglHelper(instance);   // wire LVGL to the display + touch, before anything below touches it
  instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);   // instance.begin() leaves backlight at 0

  setupGui();   // screens, gestures, quick-settings tray, Gadgetbridge -- see app_setup.cpp
}
void loop() {
  instance.loop();
  loopGui();
}
#endif
#endif
