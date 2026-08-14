# Test plan for the ported examples

## Why this document exists

These 17 sketches were written in an environment where **nothing could be compiled or run for the
target hardware**. This file records exactly what that means, so nobody mistakes "written carefully"
for "known working".

### What *was* verified

- Every LilyGoLib, SensorLib and XPowersLib symbol used here was checked against the real headers
  in `.pio/libdeps/<env>/`: `instance.drv` (`SensorDRV2605` — `setMode`, `selectLibrary`,
  `setWaveform`, `run`, `setRealtimeValue`, and the `MODE_INTTRIG`/`MODE_REALTIME` constants),
  `instance.sensor` (`SensorBMA423::enablePedometer/getPedometerCounter/resetPedometer/direction`
  and its `DIRECTION_*` enum; `SensorStepCounter`/`SensorOrientation` helpers for BHI260AP),
  `instance.rtc` (`RTC_DateTime`, `setDateTime`, `getDateTime`), `instance.pmu`
  (`getBatteryPercent`, `getBattVoltage`, `isCharging`, `isVbusIn`), and the display/touch/SD calls
  (`setBrightness`, `pushColors`, `width`, `height`, `getTouched`, `getPoint`, `hasTouch`,
  `sleepDisplay`, `wakeupDisplay`, `wakeupTouch`, `installSD`, `getDeviceProbe`).
- The board feature macros used for gating were read from `variants/lilygo_*/pins_arduino.h`.
- `PLATFORMIO_SRC_DIR` was confirmed to override `src_dir` (checked with `pio project config`).
- The two third-party libraries were checked out at the exact pinned versions and inspected:
  - **ESP8266Audio 2.0.0** — `AudioFileSourceLittleFS` exists and carries no ESP8266-only guard
    (it derives from `AudioFileSourceFS` and includes `<LittleFS.h>`); `getPos()` and `getSize()`
    are public virtuals on `AudioFileSource`.
  - **NimBLE-Arduino 2.2.3** — `onWrite(NimBLECharacteristic*, NimBLEConnInfo&)` is the correct 2.x
    signature; `NIMBLE_PROPERTY::WRITE` / `WRITE_NR` exist; `NimBLEAttValue` has an
    `operator std::string()`, so `std::string v = chr->getValue();` is valid.
- The factory app still builds (`pio run -e emulator_watch_ultra` → SUCCESS), confirming these
  additions cause no regression.

### What was *not* verified — everything else

- **No compilation.** Not for `twatchs3`, not for `twatch_ultra`. The ESP32 toolchain and most
  libraries come from `api.registry.platformio.org`, which returns HTTP 403 through this
  environment's network proxy.
- **No execution.** No sketch has run on a watch or in the emulator. The SDL emulator cannot
  substitute: it stubs out haptics, touch power modes, SD, PMU and I2S.
- **No API *semantics* checked.** Signatures were verified to exist; whether a call does what the
  sketch assumes at runtime was not. Highest-risk assumptions are listed per sketch below.

Treat every sketch as **unverified draft code**. Expect compile errors on the first pass.

---

## Stage 1 — Compile everything (do this first)

```bash
./scripts/build-examples.sh
```

Every sketch must build for **both** boards. A board-gated sketch that fails on the other board is a
defect in the `#else` stub, not an acceptable outcome.

**Likely failure points, in rough order of probability:**

| Risk | Where | Notes |
|---|---|---|
| `lv_image_dsc_t` + `LV_COLOR_FORMAT_RAW` handling | `network/HttpsGetPhoto` | LVGL 9 raw-image decoding requires the PNG decoder to be enabled in `lv_conf.h`. **Most likely remaining failure.** |
| `lv_display_get_horizontal_resolution` vs `lv_disp_get_hor_res` | all UI sketches | LilyGoLib pins LVGL ^9.4.0 on hardware; both spellings should exist, but confirm. |
| Font symbols (`lv_font_montserrat_48`, `_20`) | `ui/SimpleWatch`, `ui/ChargingAnimation` | Must be enabled in the board's `lv_conf.h`. If not, drop to `_24`/`_16`, which are used elsewhere in this project and therefore known-present. |
| `instance.loop()` needed for BHI260 event pump | `sensor/StepCount`, `sensor/Orientation` | Assumed; confirm against LilyGoLib's own BHI260 examples if the counters never move. |

---

## Stage 2 — Per-sketch hardware verification

For each: flash, open the serial monitor, and confirm both the on-screen and serial output.

```bash
PLATFORMIO_SRC_DIR=examples/<path> pio run -e <env> -t upload
PLATFORMIO_SRC_DIR=examples/<path> pio run -e <env> -t monitor
```

### haptics/DRV2605_Complex — both boards
1. Expect a *two-part* buzz once per second: a ramp that resolves into a sharp click.
2. On-screen counter increments each play.
3. **Fail mode to watch for:** a single flat pulse means the waveform slots were not applied and
   only slot 0 is playing.

### haptics/DRV2605_Realtime — both boards
1. Expect a rising-intensity ramp, then three discrete pulses, then ~1s silence, repeating.
2. Screen shows the current step and amplitude.
3. **Critical check:** the motor must be *silent* during the idle second. A continuous buzz means
   `setRealtimeValue(0)` is not stopping it, which would drain the battery.

### touch/TouchPad — both boards
1. Touch the screen: a red dot follows the finger and coordinates update.
2. Verify the dot sits *under the fingertip*, not offset — that validates the coordinate space.
3. Try two fingers; `points` should read 2 if the controller supports it.
4. **Known interaction:** LVGL and this sketch poll the same controller. If touches feel dropped,
   that contention is expected, not a bug in the read path.

### touch/TouchpanelMode — both boards
1. Countdown runs; screen blanks after 10s.
2. Tap — screen must come back **and remain responsive**.
3. **The key regression:** if the screen lights up but no further touch registers,
   `wakeupTouch()` is not doing its job. That is the whole point of the sketch.
4. Tap while awake to sleep it manually.

### sensor/StepCount — both boards (different code paths)
1. Walk 20 paces with the watch on your wrist.
2. Count should rise and roughly track reality (pedometers lag and filter; ±20% is normal).
3. **S3 path** uses the BMA423 hardware feature; **Ultra path** uses the BHI260 virtual sensor.
   Test both — they share no code.
4. Confirm the count starts at 0 on the S3 (`resetPedometer()` is called).

### sensor/Orientation — both boards
1. Rotate the watch through face-up, face-down, and each edge.
2. The reported name should change and be stable, not flicker.
3. **Note:** the two boards use *different encodings*, so the reported strings will differ between
   S3 and Ultra for the same physical position. That is expected and documented in the sketch.

### storage/DrawSD_BMP — Ultra only
1. Put an uncompressed 24-bit BMP at `/image.bmp` on the card.
2. Expect the image drawn from the top-left, top-down and right-way-up.
3. **Highest-risk sketch in the set.** Check specifically for: upside-down output (the `bottom_up`
   row-order handling), colour channels swapped (the BGR→RGB565 conversion), and skew (the 4-byte
   row-padding calculation). Any of these means the decode is wrong.
4. Test with a BMP *larger* than the screen to confirm it clips rather than crashing.
5. On the S3 build, confirm it prints the "SD card socket" message.

### storage/LVGL_FileSystem — Ultra only
1. Put a PNG at `/photo.png` on the card.
2. Expect the image centred and the caption confirming the path.
3. **If blank:** LVGL's PNG decoder may not be enabled in `lv_conf.h`. Test the driver itself
   independently by pointing an `lv_label` at a text file, or check the serial log.

### audio/PlayMP3FromFlash — both boards
1. `mkdir -p data && cp track.mp3 data/track.mp3 && pio run -e <env> -t uploadfs`
2. Expect audible playback and "Playing /track.mp3" on screen.
3. Listen for **dropouts** — they indicate `mp3->loop()` is being starved.
4. Confirm the "Finished" state at end of track.
5. Test the failure paths: no `data/` uploaded should give the "Missing" message, not a crash.

### network/StaticIPAddress — both boards
1. Edit SSID/password **and** the IP block to match your LAN.
2. Expect the configured IP shown on screen, matching `WiFi.localIP()`.
3. `ping` that address from another machine on the network.
4. **The point of the test:** confirm the address is the *static* one, not a DHCP lease. If they
   match by coincidence, change `local_ip` to something clearly outside the pool and retry.

### network/HttpsGetPhoto — both boards
1. Edit SSID/password.
2. Expect a download progress readout, then the image centred.
3. **Watch for:** `ps_malloc` failure (PSRAM absent or exhausted) and a stall mid-download (the
   20s timeout should end it rather than hanging forever).
4. Point it at a deliberately bad URL to confirm the HTTP error path is reported, not crashed.

### ble/SetTimeFromBLE — both boards
1. Expect "Advertising as T-Watch-Time" and a live clock.
2. In nRF Connect: scan, connect, find characteristic `6c5f0002-…`, write
   `2026-08-06 19:41:00` as a UTF-8 string.
3. Clock must jump to that value and keep ticking.
4. **Check the threading claim:** the RTC write happens in `loop()`, not the callback. If it crashes
   on write, that separation is wrong.
5. Send malformed input (`hello`) — must be ignored with a serial note, not crash.
6. Power-cycle and confirm the RTC retained the time.

### ui/SimpleWatch — both boards
1. Time, date, weekday, battery %, and a charge indicator when plugged in.
2. **Layout check on the 240×240 S3** — this is where crowding shows up first. Confirm nothing is
   clipped at the screen edge.
3. Confirm the time matches the RTC (set it first with `SetTimeFromBLE` or NTP).

### ui/BatmanDial — both boards
1. Three hands, sweeping correctly; second hand moves once per second.
2. **Verify 12 o'clock is at the top** — the −90° offset in `set_hand()` is the thing being tested.
   Hands pointing 90° off means that is wrong.
3. Hour hand should sit *between* hour marks as minutes advance, not jump.
4. Check the dial circle fits on both resolutions.

### ui/ChargingAnimation — both boards
1. On battery: static bar at the real percentage, blue (or red below 20%).
2. Plug USB in: the bar must start sweeping and the label read "Charging".
3. Unplug: sweep must **stop immediately**. A sweep that continues means `lv_anim_delete()` did not
   take, and the display is then lying about hardware state.
4. On a full battery with USB in, expect "USB connected (full)" rather than a sweep.

### ui/SimplePlayer — both boards
1. Upload a track as for `PlayMP3FromFlash`.
2. Play, pause, resume, stop — each must do what it says.
3. Progress bar advances during playback.
4. **Check audio quality while touching the UI.** Button handling shares `loop()` with the decoder;
   crackling on touch means the UI is starving playback.

---

## Stage 3 — Cross-cutting checks

1. **The stub path.** For each board, build a sketch that does *not* support it and confirm the
   serial message appears and the device does not hang.
2. **No regression to the factory app.** `pio run -e twatch_ultra` with the default `src_dir` must
   still build — these examples add files but must not affect it.
3. **Clean checkout.** Confirm nothing depends on the `.pio/libdeps` state of the machine they were
   written on.

## Recording results

When a sketch passes, note it in `examples/README.md`. When one fails, fix it or mark it clearly —
an example that does not work is worse than one that is absent, because it costs the next person
time before they distrust it.
