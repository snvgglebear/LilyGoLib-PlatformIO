/**
 * @file      main.cpp
 * @license   MIT
 * @brief     Native/SDL2 entry point for the testing sketch. Mirrors
 *            src/custom_interface/main.cpp; setupGui()/loopGui() are shared
 *            with testing.ino (see app_setup.h) so hardware and the emulator
 *            run identical UI code.
 *
 * Compiled only when `ARDUINO` is *not* defined, i.e. for an emulator_* env
 * pointed at this src_dir:
 *
 *     pio run -e emulator_watch_ultra -t exec
 *
 * (with `src_dir = src/testing` in platformio.ini).
 */
#ifndef ARDUINO
#include "lvgl.h"
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_keyboard.h"

#include "app_setup.h"

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

    setupGui();

    Uint32 lastTick = SDL_GetTicks();
    while (1) {
        fflush(stdout);
        SDL_Delay(5);                    // ~200 Hz cap, mirrors delay(5) on hardware
        Uint32 current = SDL_GetTicks();
        lv_tick_inc(current - lastTick); // Update the tick timer
        lastTick = current;
        loopGui();
    }
}
#endif
