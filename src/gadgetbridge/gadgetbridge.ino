/**
 * @file      gadgetbridge.ino
 * @license   MIT
 * @brief     Arduino entry point for the Gadgetbridge companion firmware.
 *
 * Implements the watch side of .claude/twatch-ultra-ble-protocol.md: advertises
 * as a T-Watch Ultra, serves the Nordic UART Service, and speaks the newline
 * delimited JSON protocol Gadgetbridge's TWATCH_ULTRA device support expects.
 *
 *     pio run -e twatch_ultra -t upload
 *
 * (after pointing src_dir at src/gadgetbridge in platformio.ini)
 *
 * As with factory.ino, this file is compiled only when `ARDUINO` is defined;
 * main.cpp in this directory is the native/SDL2 emulator counterpart, which
 * runs the same UI and protocol code with stdin standing in for the phone.
 *
 * The work is split across:
 *   gb_protocol.*  framing and the JSON message codec
 *   gb_ble.*       the BLE GATT server (NimBLE), plus DIS and Battery Service
 *   gb_app.*       watch-side state and behaviour
 *   gb_ui.*        the LVGL screens
 *   gb_platform.*  clock, battery and haptics, per board
 */
#ifdef ARDUINO

#include <LilyGoLib.h>      // board support: declares the global `instance`
#include <LV_Helper.h>      // LilyGoLib's LVGL glue (display flush + input wiring)

#include "gb_app.h"
#include "gb_link.h"
#include "gb_ui.h"

void setup()
{
    Serial.begin(115200);

    instance.begin();               // probe + init on-board peripherals
    beginLvglHelper(instance);      // wire LVGL to the display + touch
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    gb_platform::begin();           // system clock <- RTC
    gb_ui_begin();                  // screens first, so the app can refresh them
    gb_app.begin(gb_ui_on_state_changed);

    Serial.printf("[gb] ready as \"%s\", firmware %s\n", gb_link_device_name(), GB_FW_VERSION);
}

void loop()
{
    instance.loop();
    gb_app.poll();                  // drains the BLE queue, reports battery, buzzes
    lv_task_handler();
    delay(5);
}

#endif // ARDUINO
