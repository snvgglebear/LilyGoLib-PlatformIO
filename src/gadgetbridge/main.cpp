/**
 * @file      main.cpp
 * @license   MIT
 * @brief     Native/SDL2 entry point for the Gadgetbridge companion firmware.
 *
 * Counterpart to gadgetbridge.ino, mirroring src/factory/main.cpp. Compiled
 * only when `ARDUINO` is *not* defined, i.e. for an emulator_* env pointed at
 * this src_dir:
 *
 *     pio run -e emulator_watch_ultra -t exec
 *
 * There is no BLE radio here, so gb_link_stdio.cpp stands in for the phone:
 * type (or pipe) protocol lines into the terminal and they go through the same
 * framing, decoding and UI path as a real write to the RX characteristic, while
 * the watch's replies are printed to stdout. Handy for working on the screens,
 * and for checking message handling without a phone in the loop:
 *
 *     {"t":"time","ts":1786060800,"o":-300}
 *     {"t":"notify","id":8231,"src":"Signal","title":"Ada Lovelace","body":"On my way"}
 *     {"t":"call","cmd":"incoming","name":"Grace Hopper","number":"+15551234567"}
 *     {"t":"weather","temp":291,"txt":"broken clouds","loc":"Bristol"}
 *     {"t":"find","n":true}
 */
#ifndef ARDUINO

#include <stdio.h>

#include "lvgl.h"
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_keyboard.h"

#include "gb_app.h"
#include "gb_ui.h"

extern "C" int main(void)
{
    lv_init();      // must precede any other LVGL call

#ifndef WIN32
    setenv("DBUS_FATAL_WARNINGS", "0", 1); // workaround for sdl2 `-m32` crash
#endif

    // SDL_HOR_RES / SDL_VER_RES are -D build flags set per emulator env in
    // platformio.ini so the window matches the target device's panel size.
    lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    gb_platform::begin();
    gb_ui_begin();
    gb_app.begin(gb_ui_on_state_changed);

    Uint32 lastTick = SDL_GetTicks();
    while (1) {
        fflush(stdout);
        SDL_Delay(5);                    // ~200 Hz cap, mirrors delay(5) on hardware
        Uint32 current = SDL_GetTicks();
        lv_tick_inc(current - lastTick); // Update the tick timer
        lastTick = current;
        gb_app.poll();
        lv_timer_handler();
    }
}

#endif // !ARDUINO
