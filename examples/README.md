# Ported examples

Sketches brought across from the legacy
[TTGO_TWatch_Library](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library) and rewritten against
[LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib). Only demos that have **no equivalent in
LilyGoLib** and that run on **stock onboard hardware** are included.

These are rewrites, not copies. The originals used `TTGOClass::getWatch()`, TFT_eSPI, LVGL 6/7 and
an AXP202 PMU; everything here uses the global `instance`, LVGL 9, and an AXP2101.

Target boards are **T-Watch-S3** and **T-Watch-Ultra**. Every sketch compiles for both even when it
only functions on one — the unsupported board gets a stub that prints why.

## Building

Select a sketch with `PLATFORMIO_SRC_DIR`; no `platformio.ini` edit is needed.

```bash
PLATFORMIO_SRC_DIR=examples/haptics/DRV2605_Complex pio run -e twatch_ultra
PLATFORMIO_SRC_DIR=examples/haptics/DRV2605_Complex pio run -e twatch_ultra -t upload
PLATFORMIO_SRC_DIR=examples/haptics/DRV2605_Complex pio run -e twatch_ultra -t monitor
```

To compile every sketch for both boards:

```bash
./scripts/build-examples.sh
```

## The sketches

| Sketch | S3 | Ultra | What it shows |
|---|:--:|:--:|---|
| `haptics/DRV2605_Complex` | ✅ | ✅ | Chain waveform slots into one compound buzz |
| `haptics/DRV2605_Realtime` | ✅ | ✅ | Drive the motor amplitude directly (RTP mode) |
| `touch/TouchPad` | ✅ | ✅ | Raw touch coordinates without LVGL |
| `touch/TouchpanelMode` | ✅ | ✅ | Sleep the display, wake it on touch |
| `sensor/StepCount` | ✅ | ✅ | Pedometer — BMA423 feature vs BHI260 virtual sensor |
| `sensor/Orientation` | ✅ | ✅ | Which way up the watch is held |
| `storage/DrawSD_BMP` | ❌ | ✅ | Decode a 24-bit BMP and blit it to the panel |
| `storage/LVGL_FileSystem` | ❌ | ✅ | Register SD with LVGL so widgets load `S:/file` |
| `audio/PlayMP3FromFlash` | ✅ | ✅ | MP3 playback from LittleFS (no SD needed) |
| `network/StaticIPAddress` | ✅ | ✅ | Fixed IP instead of DHCP |
| `network/HttpsGetPhoto` | ✅ | ✅ | HTTPS download into PSRAM, decoded by LVGL |
| `ble/SetTimeFromBLE` | ✅ | ✅ | Set the RTC from a phone over BLE |
| `ui/SimpleWatch` | ✅ | ✅ | Digital watch face, time + date + battery |
| `ui/BatmanDial` | ✅ | ✅ | Analog face with LVGL-drawn hands |
| `ui/ChargingAnimation` | ✅ | ✅ | Battery animation driven by real PMU state |
| `ui/SimplePlayer` | ✅ | ✅ | Player UI with transport controls |
| `ui/AnalogRead` | ✅ | ✅ | Scrolling live chart of battery voltage |

The two storage sketches are Ultra-only because **the T-Watch-S3 has no SD card socket**
(`HAS_SD_CARD_SOCKET` is defined only in `variants/lilygo_twatch_ultra/pins_arduino.h`).

### Sketches needing extra setup

- `audio/PlayMP3FromFlash`, `ui/SimplePlayer` — put an MP3 at `data/track.mp3`, then
  `pio run -e <env> -t uploadfs`.
- `storage/DrawSD_BMP` — an uncompressed 24-bit BMP at `/image.bmp` on the card.
- `storage/LVGL_FileSystem` — a PNG at `/photo.png` on the card.
- `network/*` — edit `WIFI_SSID` / `WIFI_PASSWORD` at the top of the sketch.

## What was deliberately not ported

- **`BluetoothAudio` / `BluetoothAudioWeb`** — they use `btAudio`, an A2DP sink over Bluetooth
  **Classic**. The ESP32-S3 has BLE only, so these cannot work on this hardware at all.
- **Already in LilyGoLib** — `RTC`, `SDCard`, `TimeSynchronization`, `Motor`, `PlayMP3FromPROGMEM`,
  `PlayMP3FromSDToDAC`, all four `Wakeup*`, `UserButton`, `BMA423_Accel`, `BMA423_Feature`,
  `GPSDisplay`, `Microphone`, the `AXP20x_*` trio, `IRremote`, `Lvgl_Base`, `Lvgl_Button`,
  `UnitTest/*`.
- **External add-on modules** — `MAX30208`, `VEMl6075`, `ds18b20`, `Rotary`, `Fingerprint`. These
  need parts wired to an expansion connector these sealed watches do not expose.
- **Other products entirely** — everything under `T-Block/`, `LilyPi/`, `T_Bao/`, `T_Quick/`, `NES/`,
  `Shield/`, `TFT_eSPI/`, `U8g2_for_TFT_eSPI/`, `ExternTFTLibrary/`.
- **`BaiduMap`** (regional API plus a key), **`CryptocurrencyExamples`** (third-party endpoint almost
  certainly long dead), **`LilyGoGui`** (a pre-LVGL9 framework superseded by LilyGoLib's 41
  `lvgl/widgets/*` examples).

## Verification status

**None of these have been run on hardware.** See [`TEST_PLAN.md`](TEST_PLAN.md) for what has and has
not been checked, and the step-by-step procedure for validating each one.
