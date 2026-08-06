# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

PlatformIO project for [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib), targeting three ESP32-S3 LilyGo devices: T-Watch-S3(-Plus), T-Watch-Ultra, and T-LoRa-Pager. The active application (`src/factory`) is a UI-driven firmware demo built on LVGL, exercising the boards' peripherals (LoRa radios, GPS, NFC, BLE, audio, sensors, IR, etc). It doubles as the factory diagnostic/test firmware for these boards.

## Commands

Build/upload/monitor via the PlatformIO CLI (`pio`), which requires selecting an environment with `-e`:

```bash
pio run -e twatch_ultra              # build
pio run -e twatch_ultra -t upload    # build + flash over USB
pio run -e twatch_ultra -t monitor   # serial monitor
pio run -e twatchs3                  # T-Watch-S3 / S3-Plus
pio run -e tlora_pager               # T-LoRa-Pager
```

There is also a native/SDL2 emulator target that runs on the host without hardware — useful for iterating on LVGL UI without a physical board:

```bash
pio run -e emulator_watch_ultra -t exec   # build + run in one step
```

(`emulator_lora_pager` and `emulator_twatchs3` are the other emulator envs, matching screen size/inputs to their hardware counterpart.)

Only one `default_envs` may be uncommented in `platformio.ini` at a time — that's what VS Code's PlatformIO IDE build/upload/monitor buttons and `pio run` (no `-e`) act on.

There is no test suite or linter configured in this repo.

### Dev container

`.devcontainer/` provides a Docker environment with PlatformIO CLI, build-essential, and libSDL2 preinstalled (for the emulator targets) and libusb (for flashing). USB device passthrough and X11 forwarding (to view the emulator window) are commented out in `devcontainer.json` since they're host/platform-specific — uncomment as needed.

## Architecture

### Selecting what gets built

`platformio.ini` has one `[platformio]` section controlling two independent choices:

- `default_envs` — which board (`twatchs3`, `twatch_ultra`, `tlora_pager`) or emulator variant to build for. Board-specific settings (pins, partition scheme, extra libs) live in each `[env:*]` section, which `extends = env_arduino` (the shared Arduino-framework base config) or `env_emulator` (shared native/SDL2 base config).
- `src_dir` — which application source tree to compile. Defaults to `src/factory`, the main app in this repo. Commented-out alternatives point at example sketches inside the fetched `LilyGoLib` dependency (`.pio/libdeps/<env>/LilyGoLib/examples/...`) — uncomment exactly one `src_dir` line to switch.

Per-board pin mappings live in `variants/lilygo_<board>/pins_arduino.h`; board definitions (flash size, partition defaults) live in `boards/*.json`.

### `src/factory` app structure

Entry points are conditional on `ARDUINO` being defined:

- `factory.ino` — Arduino/ESP32 `setup()`/`loop()`. Initializes the board via LilyGoLib's global `instance`, WiFi/NTP, then calls `hw_init()` and `setupGui()`.
- `main.cpp` — native/SDL2 entry point (`int main()`) used only when building an `emulator_*` env, compiled instead of `factory.ino` when `ARDUINO` is undefined.

Both paths converge on the same `hw_init()` (hardware/peripheral bring-up) and `setupGui()` (LVGL UI bring-up) calls, so UI and app logic in `ui_*.cpp` files run unmodified on both real hardware and the emulator.

- `hal_interface.h`/`.cpp` — hardware abstraction layer bridging LilyGoLib's `instance` API to the app; also backfills constants/typedefs (e.g. `wl_status_t`) that only exist under `ARDUINO` so the same app code compiles natively for the emulator.
- `ui_main.cpp` — the app launcher/home screen. Each installable "app" (radio, GPS, audio, sensors, NFC, BLE, walkie-talkie, etc.) is a global `app_t` struct (`setup_func_cb`/`exit_func_cb`, see `ui_define.h`) declared `extern` and registered here via `create_app()`; the launcher shows a grid of icons and calls the matching setup/exit callback when an app is opened/closed.
- `ui_<feature>.cpp` — one file per launchable app (e.g. `ui_radio.cpp`, `ui_gps.cpp`, `ui_walkie.cpp`, `ui_nrf24.cpp`, `ui_msgchat.cpp`), each exporting its `app_t` and building its screen with the `ui_create_*`/`create_*` LVGL widget helpers declared in `ui_define.h`.
- `hw_<radio>.cpp` (`hw_sx1262`, `hw_sx1280`, `hw_lr1121`, `hw_cc1101`, `hw_nrf2401`) — RadioLib-based drivers for the interchangeable LoRa/RF radio module; which one is compiled in is selected by the `ARDUINO_LILYGO_LORA_*` build flag in `platformio.ini`.
- `app_nfc.*` — NFC (ST25R3916) protocol/event handling, used by `ui_nfc.cpp`; only active when `USING_ST25R3916` is defined (T-Watch-Ultra / T-LoRa-Pager env).
- `event_define.h` — cross-task event/message structs (e.g. NFC and audio-play events) passed between the Arduino `loop()`/background tasks and the LVGL UI.
- `audio/`, `images/`, `src/` — audio assets/codec glue, LVGL image assets (`LV_IMG_DECLARE`d in `ui_main.cpp`), and other generated/support sources for the app.
- `partitions.csv` — flash partition table used by all three hardware envs (`board_build.partitions` in `platformio.ini`).

### Radio/feature selection via build flags

`env_arduino`'s `build_flags` in `platformio.ini` both select the physical LoRa module (`ARDUINO_LILYGO_LORA_SX1262`, `_CC1101`, `_SX1280`, `_LR1121`, `_SI4432` — exactly one uncommented) and exclude unused protocols from RadioLib (`RADIOLIB_EXCLUDE_*`) to keep firmware size down. Each board env additionally defines its own `ARDUINO_T_*` board-identity macro and adds its `variants/lilygo_*` include path.

### Emulator peculiarities

`env_emulator` in `platformio.ini` builds against the `native` platform with LVGL's SDL2 backend rather than the ESP32 Arduino framework; it skips `lv_conf.h` in favor of build-flag-defined LVGL config (`LV_CONF_SKIP`/`LV_CONF_INCLUDE_SIMPLE`), and `support/sdl2_paths.py`/`support/sdl2_build_extra.py` locate/link SDL2 on the host at build time. Each `emulator_*` env sets `SDL_HOR_RES`/`SDL_VER_RES` to match its real device's screen resolution.

## Third-party dependencies

Most libraries (LilyGoLib, LVGL, RadioLib, XPowersLib, SensorLib, etc.) are pulled via `lib_deps` in `platformio.ini` into `.pio/libdeps/<env>/` — not vendored in this repo. `lib/libhelix-mp3` is the one vendored exception (MP3 decode, used by the audio app).

## Reference material

- [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) — the board support library this app is built on (`instance`, `hw_init`, board examples referenced by the commented-out `src_dir` options).
- [LVGL docs](https://lvgl.io/docs/open) and [lvgl/lvgl](https://github.com/lvgl/lvgl) — the UI toolkit used throughout `ui_*.cpp`.
- [PlatoformIO docs](https://docs.platformio.org/en/latest/) — the build system and IDE used to build/upload/monitor this project.
