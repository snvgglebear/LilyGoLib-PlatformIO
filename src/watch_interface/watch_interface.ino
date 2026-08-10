/**
 * @file      watch_interface.ino
 * @brief     Arduino entry point for the watch_interface sandbox.
 *
 * Minimal counterpart to src/factory/factory.ino, stripped down for tinkering
 * with LVGL screens on real T-Watch Ultra hardware without pulling in the
 * factory app's launcher, radio drivers, NFC, etc. Build/flash with:
 *
 *     pio run -e twatch_ultra -t upload
 *
 * (after pointing src_dir at src/watch_interface in platformio.ini)
 *
 * As with factory.ino/main.cpp, this file is compiled only when `ARDUINO` is
 * defined; main.cpp in this directory is its native/SDL2 emulator counterpart.
 */
#ifdef ARDUINO
#include <LilyGoLib.h>      // Board support: declares the global `instance` object
#include <LV_Helper.h>      // LilyGoLib's LVGL glue (display flush + input device wiring)

// Build your experimental screen(s) here.
static void setupGui()
{
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "watch_interface");
    lv_obj_center(label);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();          // probe + init on-board peripherals
    beginLvglHelper(instance); // wire LVGL to the display + touch

    setupGui();
}

void loop()
{
    instance.loop();
    lv_timer_handler();
    delay(5);
}

#endif
