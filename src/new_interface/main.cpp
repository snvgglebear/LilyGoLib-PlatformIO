/**
 * @file      main.cpp
 * @license   MIT
 * @brief     Native/SDL2 entry point -- the desktop emulator.
 *
 * Counterpart to new_interface.ino, adapted from src/factory/main.cpp the
 * same way new_interface.ino was adapted from factory.ino: same SDL2
 * display/input setup, plus usable_area_init() and app_gb_init()/
 * app_gb_poll() so the whole app -- including gadgetbridge_ble notifications,
 * calls and alarms -- is drivable here exactly as on hardware.
 *
 * There is no BLE radio on the host, so gadgetbridge_ble/gb_link_stdio.cpp
 * stands in for the phone: pipe (or type) protocol lines into the terminal
 * and they go through the same framing/decoding/UI path as a real BLE write,
 * while the watch's replies print to stdout -- see
 * src/gadgetbridge/README.md's "emulator test harness" section for example
 * lines (`{"t":"notify",...}`, `{"t":"call",...}`, etc.), which apply here
 * unchanged since it's the same protocol layer.
 *
 * Tested against `emulator_watch_ultra` (410x502, matching usable_area.h's
 * BEZEL_RADIUS calibration); it also builds for the other emulator_* envs
 * since usable_area_init() reads the live panel resolution rather than
 * assuming one, but the curved-bezel clipping it applies is only correct on
 * the Ultra's panel shape.
 *
 * There is no fourth LVGL/SDL input device for the BOOT button
 * (ui_boot_button.cpp): it reads SDL's raw keyboard-state array directly for
 * the 'B' key instead, rather than adding an lv_indev -- see that file's
 * header comment for why.
 *
 *     pio run -e emulator_watch_ultra -t exec
 *
 * @see LVGL SDL driver:  https://docs.lvgl.io/master/details/integration/driver/sdl.html
 */

#ifndef ARDUINO
#include <stdio.h>
#include "lvgl.h"
#include <unistd.h>
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_keyboard.h"

#include "hal_interface.h"
#include "app_gadgetbridge.h"
#include <usable_area.h>
#include "ui_boot_button.h"

// Same application entry points the Arduino build calls.
extern void setupGui();     // ui_main.cpp
extern void hw_init();      // hal_interface.cpp

static lv_display_t *lvDisplay;     // the SDL window, as an LVGL display
static lv_indev_t *lvMouse;         // stands in for the touchscreen
static lv_indev_t *lvMouseWheel;    // stands in for the rotary encoder (T-LoRa-Pager)
static lv_indev_t *lvKeyboard;      // stands in for the physical keyboard (T-LoRa-Pager)

#if LV_USE_LOG != 0
static void lv_log_print_g_cb(lv_log_level_t level, const char * buf)
{
    LV_UNUSED(level);
    LV_UNUSED(buf);
}
#endif

void hal_setup(void)
{
#ifndef WIN32
    setenv("DBUS_FATAL_WARNINGS", "0", 1);
#endif

#if LV_USE_LOG != 0
    lv_log_register_print_cb(lv_log_print_g_cb);
#endif

    // SDL_HOR_RES / SDL_VER_RES are -D build flags set per emulator env in
    // platformio.ini so the window matches the target device's panel size.
    lvDisplay = lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES);
    lvMouse = lv_sdl_mouse_create();
    lvMouseWheel = lv_sdl_mousewheel_create();
    lvKeyboard = lv_sdl_keyboard_create();
}

void hal_loop(void)
{
    Uint32 lastTick = SDL_GetTicks();
    while (1) {
        fflush(stdout);                  // keep printf debugging visible when piped
        SDL_Delay(5);                    // ~200 Hz cap, mirrors the delay(5) on hardware
        Uint32 current = SDL_GetTicks();
        lv_tick_inc(current - lastTick); // Update the tick timer
        lastTick = current;
        app_gb_poll();
        ui_boot_button_poll();
        lv_timer_handler();
    }
}

extern "C" int main(void)
{
    lv_init();      // must precede any other LVGL call

    hal_setup();
    printf("hello lvgl\n");

    usable_area_init();   // safe-area engine, before any UI is built

    ui_boot_button_init();   // no-op here (SDL key state needs no init), kept for parity

    hw_init();
    app_gb_init();         // gadgetbridge_ble link (gb_link_stdio.cpp on this build)
    setupGui();

    hal_loop();     // does not return
}
#endif
