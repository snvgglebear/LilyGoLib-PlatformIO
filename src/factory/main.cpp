/**
 * @file      main.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 * @brief     Native/SDL2 entry point -- the desktop emulator.
 *
 * Counterpart to factory.ino. Compiled only when `ARDUINO` is *not* defined,
 * i.e. for the `emulator_watch_ultra` / `emulator_lora_pager` /
 * `emulator_twatchs3` PlatformIO environments, which build against the `native`
 * platform instead of the ESP32 Arduino framework:
 *
 *     pio run -e emulator_watch_ultra -t exec
 *
 * Instead of a real panel and touchscreen, LVGL is pointed at its SDL2 backend:
 * a desktop window stands in for the display, and mouse / mouse wheel / keyboard
 * stand in for touch, rotary encoder, and the Pager's physical keyboard. The
 * window size comes from the SDL_HOR_RES / SDL_VER_RES build flags, which each
 * emulator env sets to its real device's resolution (see platformio.ini).
 *
 * Because the app calls the same hw_init() and setupGui() as the hardware build,
 * all UI work in ui_*.cpp can be iterated on here without flashing a board. The
 * hardware-specific half is stubbed out inside hal_interface.cpp.
 *
 * @see LVGL SDL driver:  https://docs.lvgl.io/master/details/integration/driver/sdl.html
 * @see SDL2 API:         https://wiki.libsdl.org/SDL2/APIByCategory
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
#include "demos/lv_demos.h"
#include "examples/lv_examples.h"

#include "hal_interface.h"

// Same two application entry points the Arduino build calls.
extern void setupGui();     // ui_main.cpp
extern void hw_init();      // hal_interface.cpp

static lv_display_t *lvDisplay;     // the SDL window, as an LVGL display
static lv_indev_t *lvMouse;         // stands in for the touchscreen
static lv_indev_t *lvMouseWheel;    // stands in for the rotary encoder (T-LoRa-Pager)
static lv_indev_t *lvKeyboard;      // stands in for the physical keyboard (T-LoRa-Pager)


/**
 * LVGL log sink. Deliberately empty -- LVGL's own logging is discarded so it does
 * not drown out the application's printf output on the host terminal. Drop the
 * LV_UNUSED lines and add a printf here to turn LVGL logging back on.
 */
#if LV_USE_LOG != 0
static void lv_log_print_g_cb(lv_log_level_t level, const char * buf)
{
    LV_UNUSED(level);
    LV_UNUSED(buf);
}
#endif


/**
 * Create the emulated display and input devices.
 *
 * Each lv_sdl_*_create() call registers an LVGL device backed by SDL events, so
 * from the application's point of view they are indistinguishable from the real
 * touch panel / encoder / keypad registered by beginLvglHelper() on hardware.
 */
void hal_setup(void)
{
    // Workaround for sdl2 `-m32` crash
    // https://bugs.launchpad.net/ubuntu/+source/libsdl2/+bug/1775067/comments/7
#ifndef WIN32
    setenv("DBUS_FATAL_WARNINGS", "0", 1);
#endif

#if LV_USE_LOG != 0
    lv_log_register_print_cb(lv_log_print_g_cb);
#endif

    /* Add a display
     * Use the 'monitor' driver which creates window on PC's monitor to simulate a display*/

    // SDL_HOR_RES / SDL_VER_RES are -D build flags set per emulator env in
    // platformio.ini so the window matches the target device's panel size.
    lvDisplay = lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES);
    lvMouse = lv_sdl_mouse_create();
    lvMouseWheel = lv_sdl_mousewheel_create();
    lvKeyboard = lv_sdl_keyboard_create();
}

/**
 * Desktop equivalent of the Arduino loop(), but it never returns -- the process
 * exits when the SDL window is closed.
 *
 * On hardware a timer ISR advances LVGL's millisecond tick; on the host there is
 * no such interrupt, so the elapsed wall-clock time from SDL_GetTicks() is fed
 * to lv_tick_inc() manually each pass. Getting this wrong makes every LVGL
 * animation and timer run at the wrong speed.
 *
 * No mutex is needed here (unlike factory.ino) because the emulator build has no
 * background tasks contending for the hardware.
 *
 * @see https://docs.lvgl.io/master/details/integration/adding-lvgl-to-your-project/timer-handler.html
 */
void hal_loop(void)
{
    Uint32 lastTick = SDL_GetTicks();
    while (1) {
        fflush(stdout);                  // keep printf debugging visible when piped
        SDL_Delay(5);                    // ~200 Hz cap, mirrors the delay(5) on hardware
        Uint32 current = SDL_GetTicks();
        lv_tick_inc(current - lastTick); // Update the tick timer. Tick is new for LVGL 9
        lastTick = current;
        lv_timer_handler(); // Update the UI-
    }
}


/**
 * Native entry point. Mirrors setup() + loop() from factory.ino:
 * lv_init() and hal_setup() replace instance.begin() / beginLvglHelper(), then
 * the identical hw_init() + setupGui() pair builds the application.
 *
 * Declared `extern "C"` so the linker finds the C-linkage `main` symbol expected
 * by the C runtime even though this translation unit is compiled as C++.
 */
extern "C" int main(void)
{
    lv_init();      // must precede any other LVGL call

    hal_setup();
    printf("hello lvgl\n");
    //****************** */
    hw_init();
    setupGui();


    //****************** */
    hal_loop();     // does not return
}
#endif
