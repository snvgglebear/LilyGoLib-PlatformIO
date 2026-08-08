# Configuration Reference

Every configuration value and tunable constant in this project, with where it
lives and what changing it does.

Configuration arrives from four layers, in the order the build resolves them:

| Layer | Where | What it decides |
|---|---|---|
| 1. Build selection | `platformio.ini` | Which board, which app source tree, which radio module |
| 2. Board definition | `boards/*.json`, `variants/lilygo_*/pins_arduino.h` | Flash size, CPU clock, pin assignments |
| 3. Capability matrix | `src/factory/hal_interface.h` (bottom of file) | Which peripherals and apps exist in this image |
| 4. Runtime constants | `src/factory/*.cpp` / `*.h` | Timings, defaults, buffer sizes, UI geometry |

A value defined at a lower-numbered layer generally feeds the ones below it —
for example the `-D ARDUINO_T_LORA_PAGER` flag chosen in layer 1 is what selects
a whole branch of layer 3.

---

## 1. Build selection (`platformio.ini`)

### Target board — `default_envs`

Exactly one `default_envs` line may be uncommented. It sets what the VS Code
PlatformIO buttons and a bare `pio run` act on; `-e <env>` overrides it per
invocation.

| Environment | Board | Panel |
|---|---|---|
| `twatchs3` | T-Watch-S3 / S3-Plus | 240 × 240 |
| `twatch_ultra` | T-Watch-Ultra *(current default)* | 502 × 410 |
| `tlora_pager` | T-LoRa-Pager | 480 × 222 |
| `emulator_twatchs3` | Desktop SDL2 | 240 × 240 |
| `emulator_watch_ultra` | Desktop SDL2 | 502 × 410 |
| `emulator_lora_pager` | Desktop SDL2 | 480 × 222 |

### Application source — `src_dir`

Also exactly one line. Defaults to `src/factory`, this repo's application.
The commented alternatives point into the fetched LilyGoLib dependency
(`.pio/libdeps/<env>/LilyGoLib/examples/...`) and only resolve after a first
build has downloaded it.

### Radio module — `ARDUINO_LILYGO_LORA_*`

Exactly one uncommented, in `[env_arduino]` `build_flags`. Selects which
`hw_*.cpp` driver compiles; the other four become empty translation units.

| Flag | Driver file | Band | Max power | Notes |
|---|---|---|---|---|
| `ARDUINO_LILYGO_LORA_SX1262` *(default)* | `hw_sx1262.cpp` | 150–960 MHz | +22 dBm | LoRa, longest range |
| `ARDUINO_LILYGO_LORA_SX1280` | `hw_sx1280.cpp` | 2.4 GHz | +13 dBm | LoRa, higher data rate, shorter range |
| `ARDUINO_LILYGO_LORA_LR1121` | `hw_lr1121.cpp` | dual-band | +22 / +13 dBm | Sub-GHz **and** 2.4 GHz |
| `ARDUINO_LILYGO_LORA_CC1101` | `hw_cc1101.cpp` | 387–464 MHz (as listed) | +10 dBm | **(G)FSK, not LoRa** — `sf`/`cr` unused |
| `ARDUINO_LILYGO_LORA_SI4432` | *(no driver in this tree)* | — | — | Flag exists; no `hw_si4432.cpp` here |

### RadioLib exclusions — `RADIOLIB_EXCLUDE_*`

Each flag drops a protocol or chip family from RadioLib to shrink the image.
Currently excluded: `RF69`, `SX1231`, `RFM2X`, `SX127X`, `AFSK`, `AX25`,
`HELLSCHREIBER`, `MORSE`, `RTTY`, `SSTV`, `DIRECT_RECEIVE`, `APRS`, `BELL`.

Left available (commented out): `CC1101`, `SI443X`, `SX128X`, `NRF24`,
`STM32WLX`.

> `RADIOLIB_EXCLUDE_NRF24` has a side effect: `hal_interface.h` only defines
> `USING_EXTERN_NRF2401` when it is *absent*, so excluding nRF24 also removes
> the nRF24 app from the T-LoRa-Pager launcher.

### Other build settings

| Setting | Value | Meaning |
|---|---|---|
| `platform` | `espressif32@6.10.0` | Pinned; the Walkie app additionally requires arduino-esp32 ≤ 3.0.0 |
| `upload_speed` | `921600` | USB flashing baud |
| `monitor_speed` | `115200` | Must match `Serial.begin()` in `factory.ino` |
| `board_build.filesystem` | `fatfs` | Internal filesystem format |
| `board_build.partitions` | `src/factory/partitions.csv` | See §2 |
| `CORE_DEBUG_LEVEL` | `0` | Arduino core log verbosity; raise to 3–5 for `log_d`/`log_v` output |
| `-Wnarrowing` | — | Warns on implicit narrowing conversions |

### Emulator-only flags (`[env_emulator]`)

| Flag | Value | Meaning |
|---|---|---|
| `SDL_HOR_RES` / `SDL_VER_RES` | per env, see table above | SDL window size, consumed by `main.cpp` |
| `SDL_ZOOM` | `1` | Window scale factor |
| `LV_CONF_SKIP` | — | Ignore `lv_conf.h`; configure LVGL from build flags instead |
| `LV_MEM_CUSTOM` / `LV_MEM_SIZE` | `1` / `600×480×2` (≈576 KB) | LVGL heap on the host |
| `LV_FONT_MONTSERRAT_*` | `12,14,16,18,20,21,22,24,28,32,48` | Fonts compiled in |
| `LV_USE_FLOAT`, `LV_USE_PRIVATE_API`, `LV_USE_SDL` | `1` | Required by the SDL backend and this app |
| `LV_LOG_PRINTF` | `1` | LVGL logging (discarded by the empty sink in `main.cpp`) |

---

## 2. Board and hardware definitions

### Flash partitions — `src/factory/partitions.csv`

Shared by all three hardware environments. Total 16 MB.

| Name | Type | Offset | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | 20 KB | Key/value store — holds `user_setting_params_t` |
| `otadata` | data/ota | `0xE000` | 8 KB | Which OTA slot to boot |
| `app0` | app/ota_0 | `0x10000` | 4 MB | Firmware slot A |
| `app1` | app/ota_1 | `0x410000` | 4 MB | Firmware slot B |
| `ffat` | data/fat | `0x810000` | ~7.9 MB | Audio files and assets |
| `coredump` | data/coredump | `0xFF0000` | 64 KB | Panic dumps |

Two 4 MB app slots means OTA is possible but a single image cannot exceed 4 MB.

### Board JSON — `boards/*.json`

| Key | Value (all three boards) |
|---|---|
| `mcu` | `esp32s3` |
| `f_cpu` | 240 MHz |
| `f_flash` / `flash_mode` | 80 MHz, QIO |
| `flash_size` | 16 MB |
| `maximum_ram_size` | 327680 (320 KB internal SRAM; PSRAM is separate) |
| `extra_flags` | `BOARD_HAS_PSRAM`, `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_RUNNING_CORE=1` |

`memory_type` differs: `qio_qspi` on the T-LoRa-Pager, `qio_opi` on the watches.

> `ARDUINO_USB_CDC_ON_BOOT=1` is what makes `Serial` reach the USB port. The
> commented `-U ARDUINO_USB_CDC_ON_BOOT` in `platformio.ini` disables serial output.

### Pin assignments — `variants/lilygo_*/pins_arduino.h`

Per-board pin maps. `GPS_PPS` is referenced directly by `hw_gps_attach_pps()`
in `hal_interface.cpp` and is `#ifdef`-guarded, so a board that does not define
it silently skips PPS monitoring.

---

## 3. Capability matrix (`src/factory/hal_interface.h`)

The block at the bottom of the file derives capability macros from the single
`ARDUINO_T_*` board flag. This is the table that decides which apps appear and
which drivers compile.

| Macro | T-Watch-S3 | T-Watch-Ultra | T-LoRa-Pager |
|---|:--:|:--:|:--:|
| `USING_TOUCHPAD` | ✅ | ✅ | — |
| `USING_BLE_KEYBOARD` | ✅ ¹ | ✅ | ✅ |
| `USING_BHI260_SENSOR` | — | ✅ | ✅ |
| `USING_BMA423_SENSOR` | ✅ | — | — |
| `USING_ST25R3916` (NFC) | — | ✅ | ✅ |
| `USING_EXTERN_NRF2401` | — | — | ✅ ² |
| `HAS_USB_RF_SWITCH` | — | ✅ | — |
| `DEVICE_KEYBOARD_TYPE` | — | — | `KEYBOARD_TYPE_1` |
| `FLOAT_BUTTON_WIDTH` / `HEIGHT` | 40 × 40 | 80 × 80 | 40 × 40 |
| `MAIN_FONT` | `montserrat_12` | `montserrat_22` | `montserrat_16` |
| `NFC_TIPS_STRING` | "No NFC devices" | front of screen | centre of arrow on back |

¹ On T-Watch-S3 only, `USING_BLE_KEYBOARD` is nested inside the
`#ifndef USING_BMA423_SENSOR` guard — a build that defines `USING_BMA423_SENSOR`
externally will not get it.
² Also requires `RADIOLIB_EXCLUDE_NRF24` to be absent.

Most guards use `#ifndef`, so these can be forced on from `platformio.ini`
build flags without a redefinition error.

### Capability macros defined elsewhere

These are set by LilyGoLib or by env build flags rather than the block above, and
gate code throughout `hal_interface.cpp`:

`USING_AUDIO_CODEC`, `USING_PCM_AMPLIFIER`, `USING_PDM_MICROPHONE`,
`USING_PMU_MANAGE`, `USING_PPM_MANAGE`, `USING_BQ_GAUGE`, `USING_XL9555_EXPANDS`,
`USING_SI473X_RADIO`, `USING_BME280`, `USING_MAG_QMC5883`, `USING_QMI8658_SENSOR`,
`USING_INPUT_DEV_KEYBOARD`, `USING_INPUT_DEV_TOUCHPAD`, `USING_INPUT_DEV_ROTARY`,
`USING_TRACKBALL`, `USING_IR_REMOTE`, `USING_UART_BLE`, `USING_BLE_CONTROL`,
`USING_LED_INDICATOR`, `HAS_SD_CARD_SOCKET`, `USING_FATFS`.

### Peripheral probe bits — `HW_*_ONLINE`

`hw_get_device_online()` returns a bitmask of peripherals that answered at boot.
Bit positions must stay in step with the `hw_devices[]` name array in
`hal_interface.cpp`.

| Bit | Macro | Bit | Macro |
|---|---|---|---|
| 0 | `HW_RADIO_ONLINE` | 11 | `HW_GAUGE_ONLINE` |
| 1 | `HW_TOUCH_ONLINE` | 12 | `HW_EXPAND_ONLINE` |
| 2 | `HW_DRV_ONLINE` | 13 | `HW_CODEC_ONLINE` |
| 3 | `HW_PMU_ONLINE` | 14 | `HW_NRF24_ONLINE` |
| 4 | `HW_RTC_ONLINE` | 15 | `HW_SI473X_ONLINE` |
| 5 | `HW_PSRAM_ONLINE` | 16 | `HW_BME280_ONLINE` |
| 6 | `HW_GPS_ONLINE` | 17 | `HW_QMC5883P_ONLINE` |
| 7 | `HW_SD_ONLINE` | 18 | `HW_BMA423_ONLINE` |
| 8 | `HW_NFC_ONLINE` | 19 | `HW_QMI8658_ONLINE` |
| 9 | `HW_BHI260AP_ONLINE` | 20 | `HW_LED_INDIC_ONLINE` |
| 10 | `HW_KEYBOARD_ONLINE` | | |

---

## 4. Runtime constants

### Most likely to need changing

| Constant | File | Default | Notes |
|---|---|---|---|
| `GMT_OFFSET_SECOND` | `hal_interface.h` | `8*3600` (UTC+8) | **Change this for your timezone.** No DST handling. Defined unconditionally, so a `-D` build flag collides rather than overrides — edit the line. |
| `RADIO_DEFAULT_FREQUENCY` | `hal_interface.h` | `916.0` MHz | **Check this is legal in your region before transmitting.** |
| `RADIO_FIXED_FREQUENCY` | `hal_interface.h` | commented out | Uncomment (with `RADIO_FIXED_FREQUENCY_STRING`) to lock the radio to one frequency |

### Time and system

| Constant | File | Default | Meaning |
|---|---|---|---|
| `SCREEN_TIMEOUT` | `ui_main.cpp` | 10000 ms | Idle time before the launcher gives way to the watch face |
| `NVS_NAME` | `hal_interface.cpp` | `"pager"` | NVS namespace for saved settings; changing it orphans existing settings |
| CPU frequency | `factory.ino` / `ui_main.cpp` | 240 MHz active, 80 MHz on the watch face | Set via `setCpuFrequencyMhz()` / `hw_set_cpu_freq()` |
| Loop delay | `factory.ino` | 5 ms | Caps the UI at ~200 Hz and yields to the idle task |
| Arduino loop stack | `hal_interface.cpp` | 30 KB | Raised from the 8 KB default via `getArduinoLoopTaskStackSize()` |
| Splash duration | `ui_main.cpp` | 5000 ms | Blocking |
| Low-battery cutoff | `ui_main.cpp` | < 3300 mV **and** USB absent | Shows a warning for 3 s, then shuts down |

### Persisted user settings — `user_setting_params_t`

First-boot defaults from `hw_init()` in `hal_interface.cpp`; thereafter loaded
from NVS.

| Field | Hardware default | Emulator default | Range |
|---|---|---|---|
| `brightness_level` | 50 | 10 | 0–255 (watches), 0–16 (Pager) |
| `keyboard_bl_level` | 80 | 255 | Pager only |
| `disp_timeout_second` | 30 | 30 | 0 disables display-off entirely |
| `charger_current` | `DEVICE_CHARGE_CURRENT_RECOMMEND` | 1000 mA | See below |
| `charger_enable` | `true` | `true` | — |

> Validation on load is a size check only. A change to the struct layout is
> detected as a wrong-sized read and silently resets every setting to default.

### Device limits (emulator fallbacks — hardware values come from LilyGoLib)

| Constant | `hal_interface.h` | Meaning |
|---|---|---|
| `DEVICE_MAX_BRIGHTNESS_LEVEL` | 255 | Backlight ceiling |
| `DEVICE_MIN_BRIGHTNESS_LEVEL` | 0 | Backlight floor |
| `DEVICE_MAX_CHARGE_CURRENT` | 1000 mA | Charge current ceiling |
| `DEVICE_MIN_CHARGE_CURRENT` | 100 mA | Charge current floor |
| `DEVICE_CHARGE_LEVEL_NUMS` | 12 | Selectable charge steps in the UI |
| `DEVICE_CHARGE_STEPS` | 1 | Increment between steps |

PMU charge-current ladder (`hw_set_charger_current_level()`, in mA):
`100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000`.
On PPM-managed boards the current is `level × DEVICE_CHARGE_STEPS` instead.

### Audio

| Constant | File | Default | Meaning |
|---|---|---|---|
| `FFT_SIZE` | `hal_interface.h` | 512 | Samples per transform; power of two required |
| `SAMPLE_RATE` | `hal_interface.h` | 16000 Hz | Nyquist limit 8 kHz |
| `FREQ_BANDS` | `hal_interface.h` | 16 | Display bars; 500 Hz per band at defaults |
| `mic_gain` | `hal_interface.cpp` | 10 | Codec preamp gain (same on all boards) |
| Player task | `hal_interface.cpp` | 8 KB stack, priority 12 | Above the Arduino loop task (priority 1) |
| `playerQueue` depth | `hal_interface.cpp` | 2 | Play requests that may be outstanding |
| Codec volume at init | `hal_interface.cpp` | 100 | — |
| Notification volume | `ui_msgchat.cpp` | 70 | Set before playing `/notification.mp3` |
| Keypress feedback | `hal_interface.cpp` | 80 ms | Haptic pulse length |

FFT display scaling: magnitudes are converted to dBFS and mapped through
`(dB + 40) / 40`, so the visible window is −40…0 dBFS. Banding is **linear** in
frequency, not logarithmic.

### Radio option lists

Per driver, since the valid values are chip-specific. Each list exists twice —
as a newline-separated caption string and as a numeric array — and the two must
stay the same length and order.

**SX1262** (`hw_sx1262.cpp`)
- Frequency (MHz): `433, 470, 842, 850, 868, 915, 923, 945`
- Bandwidth (kHz): `41.7, 62.5, 125, 250, 500`
- Power (dBm): `2, 5, 10, 12, 17, 20, 22`
- Current limit: 140 mA (PA over-current protection)

**SX1280** (`hw_sx1280.cpp`)
- Frequency (MHz): `2400, 2412, 2422, 2432, 2442, 2452, 2462, 2472, 2482, 2492, 2500`
- Bandwidth (kHz): `203.125, 406.25, 812.5, 1625`
- Power (dBm): `0`–`13`

**LR1121** (`hw_lr1121.cpp`) — two sets, selected by `_high_freq`
- Frequency (MHz): `315, 433, 434, 470, 842, 850, 868, 915, 923, 945` then `2400`–`2500`
- Bandwidth sub-GHz (kHz): `62.5, 125, 250, 500`
- Bandwidth 2.4 GHz (kHz): `62.5, 125, 203.125, 250, 406.25, 500, 812.5`
- Power sub-GHz (dBm): `2, 5, 10, 12, 17, 20, 22`
- Power 2.4 GHz (dBm): `0`–`13`

**CC1101** (`hw_cc1101.cpp`)
- Frequency (MHz): `387, 400, 410, 420, 433, 440, 450, 460, 464` — the offered
  list covers only the lower part of the chip's tuning range
- RX filter bandwidth (kHz): `0.025, 5, 10, 20, 30, 60, 80, 100, 120, 150, 200, 300, 400, 500, 600`
- Power (dBm): `-30, -20, -15, -10, 0, 5, 7, 10`
- `RADIO_DEFAULT_BIT_RATE` 38.4 kbps, `RADIO_DEFAULT_DEV_FREQ` 20.0 kHz deviation

**nRF24** (`hw_nrf2401.cpp`, `ui_nrf24.cpp`)
- Frequency (MHz): `2400, 2424, 2450, 2500, 2525`
- Power (dBm): `-18, -12, -6, 0`
- Bit rate (kbps): `1000, 2000, 250` — note the list order
- TX interval (ms): `1000, 2000, 3000`
- Pipe address: `{0x01, 0x23, 0x45, 0x67, 0x89}` — hard-coded, so every device
  running this firmware shares one address

### LoRa defaults (`hw_get_radio_params()`, SX1262)

| Field | Default | Effect |
|---|---|---|
| `freq` | `RADIO_DEFAULT_FREQUENCY` (916 MHz) | — |
| `bandwidth` | 125.0 kHz | — |
| `sf` | 12 | Maximum spreading factor: longest range, slowest, seconds of air-time per packet |
| `cr` | 5 (4/5) | Lightest forward error correction |
| `power` | 22 dBm | Module maximum |
| `syncWord` | `0xCD` | Private-network sync word (`0x34` is reserved for LoRaWAN) |
| `interval` | 3000 ms | Between automatic transmissions |
| `mode` | `RADIO_DISABLE` | — |

### UI options

| Constant | File | Default |
|---|---|---|
| LoRa TX interval choices | `ui_radio.cpp` | `100, 200, 500, 1000, 2000, 3000` ms |
| Spreading factor choices | `ui_radio.cpp` | `5`–`12` |
| Coding rate choices | `ui_radio.cpp` | `5`–`8` (4/5 … 4/8) |
| Band-switch threshold | `ui_radio.cpp` | > 960 MHz sets `_high_freq` |
| `MAX_MSG_COUNT` | `ui_msgchat.cpp` | 20 chat bubbles kept |
| `SHOOT_KEY` | `ui_camera_remote.cpp` | `MEDIA_VOLUME_UP` |
| `nec_code` default | `ui_ir_remote.cpp` | `0x12345678` |
| `DEFAULT_OPA` | `ui_define.h` | 100 |
| Bar geometry | `ui_microphone.cpp` | 8 × 75 px, 2 px spacing, 90 px channel offset |
| Bar hue range | `ui_microphone.cpp` | left 180–360°, right 0–180°, S/V 100 |
| WiFi connect timeout | `ui_msg.cpp` | 10000 ms |
| Screen-test interval | `ui_factory.cpp` | 3000 ms per pattern |

### Timer intervals

| Timer | File | Interval |
|---|---|---|
| FFT display refresh | `ui_microphone.cpp` | 1 ms (as fast as LVGL allows) |
| Walkie-talkie loop | `ui_walkie.cpp` | 100 ms |
| IMU refresh | `ui_sensor.cpp` | 100 ms |
| LoRa chat receive poll | `ui_msgchat.cpp` | 300 ms |
| Audio playback state | `ui_audio.cpp` | 500 ms |
| WiFi progress bar | `ui_msg.cpp` | 500 ms |
| Clock face | `ui_main.cpp` | 1000 ms |
| Idle/power state machine | `ui_main.cpp` | 1000 ms |
| GPS / monitor / settings / BLE | respective files | 1000 ms |
| Radio & nRF24 test loop | `ui_radio.cpp`, `ui_nrf24.cpp` | 1000 ms |
| BLE keyboard state | `ui_ble_kb.cpp` | 3000 ms |
| Screen test patterns | `ui_factory.cpp` | 3000 ms |
| Low-battery check | `ui_main.cpp` | 5000 ms |

### Walkie-talkie (ESP-NOW, `ui_walkie.cpp`)

Only compiled for `ARDUINO_T_LORA_PAGER` with arduino-esp32 ≤ 3.0.0.

| Constant | Default | Meaning |
|---|---|---|
| `kSampleRate` | 16000 Hz | Wideband speech |
| `kChannels` / `kBitsPerSample` | 1 / 16 | Mono PCM |
| `kFrameSamples` / `kFrameBytes` | 320 / 160 | 20 ms G.722 frame @ 64 kbps |
| `kWifiChannel` | 1 | Default when WiFi is offline; locked to the WiFi channel otherwise |
| `kRxQueueDepth` | 8 | 160 ms of buffered audio |
| `kAudioBufferFrames` | 2048 | PCMFlow jitter buffer |
| `kTaskStack` / `kTaskPrio` | 4096 / 10 | Above the Arduino loop task |
| `kNickMax` | 20 | Nickname length, including NUL |
| `kHelloMagic` | `"WLKH"` | Marks a control packet |
| `kAnnounceMs` | 2000 ms | Peer announcement period |
| `kContactTtlMs` | 15000 ms | Peer dropped after ~7 missed announcements |
| `kHelloQueueLen` | 8 | Queued peer announcements |
| Broadcast MAC | `FF:FF:FF:FF:FF:FF` | Audio goes to every peer on the channel |

### NFC (`app_nfc.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `rawBuffer` | 1024 bytes | Largest NDEF message readable; longer tags are truncated |
| `devLimit` | 1 | First tag only; no multi-tag anticollision |
| `techs2Find` | `RFAL_NFC_POLL_TECH_A` | NFC-A / ISO14443-A only (MIFARE, NTAG). Type B and Felica ignored |
| `totalDuration` | 1000 ms | Discovery cycle length |
| `wakeupEnabled` | `false` | Active polling — more responsive, higher current draw |
| `DEBUG_NDEF` | undefined | Define for verbose record dumps. **Prints WiFi keys in clear text** |

### Internal event bits

| Group | Bits |
|---|---|
| Audio player (`hal_interface.cpp`) | `PLAYER_PLAY` (0), `PLAYER_END` (1), `PLAYER_RUNNING` (2) |
| LoRa radio (`hw_*.cpp`) | `LORA_ISR_FLAG` (0) |
| nRF24 (`hw_nrf2401.cpp`) | `NRF24_ISR_FLAG` (1) — deliberately distinct, both radios can be active |

### LVGL message IDs (`ui_define.h`)

Used to push data from background producers into LVGL safely. Values are
arbitrary but must stay unique; they are grouped by hundreds per subsystem.

| ID | Value | Meaning |
|---|---|---|
| `MSG_MENU_NAME_CHANGED` | 100 | Menu page title changed |
| `MSG_LABEL_PARAM_CHANGE_1` / `_2` | 200 / 201 | Generic label value update |
| `MSG_TITLE_NAME_CHANGE` | 203 | Screen title changed |
| `MSG_BLE_SEND_DATA_1` / `_2` | 204 / 205 | BLE payload received |
| `MSG_MUSIC_TIME_ID` | 300 | Playback position tick |
| `MSG_MUSIC_TIME_END_ID` | 301 | Playback finished |
| `MSG_FFT_ID` | 400 | New FFT bin data |

---

## Common changes

**Set your timezone** — edit `GMT_OFFSET_SECOND` in `src/factory/hal_interface.h`.
For example `(-5*3600)` for US Eastern Standard Time.

**Set a legal radio frequency** — edit `RADIO_DEFAULT_FREQUENCY` in
`src/factory/hal_interface.h`, and check the frequency list in the active
`hw_*.cpp` matches what is permitted where you are.

**Build for a different board** — uncomment one `default_envs` in
`platformio.ini` (leaving all others commented), or pass `-e <env>` on the
command line.

**Switch radio module** — uncomment exactly one `ARDUINO_LILYGO_LORA_*` flag in
`[env_arduino]` `build_flags`.

**Enable serial debug output** — raise `CORE_DEBUG_LEVEL` from `0` to `3`–`5`,
and make sure `-U ARDUINO_USB_CDC_ON_BOOT` stays commented out.

**Change the screen timeout** — `SCREEN_TIMEOUT` in `ui_main.cpp` controls the
time before the watch face appears; the subsequent display-off delay is the
user-facing `disp_timeout_second` setting.

**Recover more flash for the app** — the 4 MB app slots in `partitions.csv` are
sized for OTA. Dropping to a single-app layout would free ~4 MB, at the cost of
over-the-air updates.

---

## Cross-references

- [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) — board support library
- [LVGL documentation](https://docs.lvgl.io/master/) — UI toolkit
- [RadioLib API](https://jgromes.github.io/RadioLib/) — radio drivers
- [PlatformIO project configuration](https://docs.platformio.org/en/latest/projectconf/index.html)
- [ESP-IDF partition tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/partition-tables.html)
- [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html)
