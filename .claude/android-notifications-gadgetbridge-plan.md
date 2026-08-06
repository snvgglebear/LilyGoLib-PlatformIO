# Plan: Android Notifications on T-Watch Ultra via Gadgetbridge

## Goal

Receive Android notifications (app, title, body) on the T-Watch Ultra as a popup + vibration, using [Gadgetbridge](https://github.com/Freeyourgadget/Gadgetbridge) on the phone side instead of a hand-rolled companion app.

Two independent halves have to be built and kept in sync: a BLE GATT server on the watch, and a new `DeviceCoordinator`/`DeviceSupport` pair in a Gadgetbridge fork.

## Current state (verified 2026-08-05)

- `src/factory/ui_ble.cpp` already has a BLE on/off + "message receiving window" screen, **but it is invisible today**: the launcher entry is gated on `#if defined(USING_UART_BLE)` (`src/factory/ui_main.cpp:645`) and that macro is defined nowhere in the project. Defining it is what both reveals the app and compiles the stubbed code paths in.
- `src/factory/hal_interface.cpp:1655-1682` — `hw_enable_ble()`, `hw_disable_ble()`, `hw_deinit_ble()`, `hw_get_ble_message()` are **empty stubs**, gated behind `#if defined(ARDUINO) && defined(USING_UART_BLE)` with nothing inside. No BLE server runs today.
- `h2zero/NimBLE-Arduino @ ^2.2.3` is already a direct dependency in `platformio.ini` `lib_deps`, so the BLE stack is available, just unused. Note it's the **2.x** API — most online examples show the older 1.x callback signatures.
- LVGL is `lvgl/lvgl @ ^9.4.0` (`platformio.ini`; the emulator env pins 9.2.2). UI helpers in `src/factory/ui_tools.cpp` are version-gated with `#if LVGL_VERSION_MAJOR == 9`.
- `src/factory/ui_msg.cpp` has a working `ui_msg_pop_up(title, msg)` popup, currently used for Wi-Fi status — reusable for notifications. **Known defect for this use case**: the LVGL 9 branch of `create_msgbox()` (`src/factory/ui_tools.cpp:127`) never calls `lv_msgbox_add_title()`, so the title argument is silently dropped on this build. One-line fix (see Phase 1).
- Small pre-existing bug to fix while in there: `ui_ble.cpp`'s poll timer does `buffer[rs + 1] = '\0'` — off by one; should be `buffer[rs] = '\0'` (and can write past the 256-byte buffer when a message fills it).
- Gadgetbridge has no T-Watch Ultra support. Closest architectural reference in its codebase is `AsteroidOSDeviceCoordinator` / `AsteroidOSDeviceSupport` (`app/src/main/java/nodomain/freeyourgadget/gadgetbridge/{devices,service/devices}/asteroidos/`) — a simple, open, notification-focused BLE watch protocol. Use it as the template, not a dependency.

## Wire protocol (define first, both sides depend on it)

Pick fixed UUIDs and a payload format before writing code on either side — this is the contract between the two halves.

- **Service UUID** and **Notification characteristic UUID**: generate two random UUIDs (e.g. via `uuidgen` / `python3 -c "import uuid; print(uuid.uuid4())"`) and hardcode them identically in the firmware and in the Gadgetbridge device support class. Do not reuse the Nordic UART Service UUID — a dedicated service lets Gadgetbridge's `createBLEScanFilters()` identify the watch unambiguously.
- **Payload**: a compact delimited string is easiest to parse in C++ without a JSON library — e.g. `appName\x1Ftitle\x1Fbody` (`\x1F` = ASCII unit separator, unlikely to appear in notification text). Match `AsteroidOSNotification.toString()` for inspiration if you want structure later (id, vibration pattern, actions).
- **BLE MTU**: default ATT MTU is 23 bytes (20 usable). Either negotiate a larger MTU on connect (Gadgetbridge's `AbstractBTLEDeviceSupport` supports this) or chunk/truncate long notification bodies on the phone side before writing.
- **Direction**: phone → watch only, for v1. No ack, no delete-notification, no music/call support yet (stretch goals below).

## Phase 1 — Firmware: implement the BLE GATT server

Files: `src/factory/hal_interface.cpp`, `src/factory/hal_interface.h`, `src/factory/ui_ble.cpp`, `src/factory/ui_tools.cpp`, `platformio.ini`.

1. Enable the feature flag: add `-D USING_UART_BLE` to `build_flags` in `platformio.ini` (or `#define USING_UART_BLE` in the `ARDUINO_T_WATCH_S3_ULTRA` block of `hal_interface.h`, ~line 1534, next to `USING_BLE_KEYBOARD`). This makes the "Bluetooth" app appear in the launcher (`ui_main.cpp:645`) and compiles the stubbed `hw_*_ble` paths in.
2. In `hw_enable_ble(devName)`: start a NimBLE server, create the service + notification characteristic (write-only from central), set the advertised name to `devName`, start advertising. Use the NimBLE **2.x** callback signature: `onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo)`.
3. In the write callback, copy the raw payload into a small FreeRTOS queue (or mutex-guarded ring buffer) and return. **Do not touch LVGL from this callback** — it runs on the NimBLE host task and LVGL is not thread-safe; the only safe place to build UI is the LVGL timer that already polls `hw_get_ble_message()`. The existing pull model isn't just less code, it's the correct threading design — keep it.
4. In `hw_disable_ble()` / `hw_deinit_ble()`: stop advertising and tear down the server cleanly (matches the existing on/off toggle in `ui_ble.cpp`).
5. In `ui_ble.cpp`'s polling timer callback: fix the `buffer[rs + 1]` off-by-one, then split the `appName\x1Ftitle\x1Fbody` payload and call `ui_msg_pop_up(title, body)` plus a haptic tick via `hw_feedback()`.
6. Fix the popup title: in the `LVGL_VERSION_MAJOR == 9` branch of `create_msgbox()` (`ui_tools.cpp:127`), add `lv_msgbox_add_title(msgbox, title_txt);` — the function exists in LVGL 9 ([msgbox docs](https://lvgl.io/docs/open/widgets/msgbox)) but the current code only calls `lv_msgbox_add_text()`, so titles never render. (`lv_msgbox_create(NULL)` already makes the box modal on the top layer — no change needed there.)
7. Bench test with a generic BLE tool (e.g. `nRF Connect` on Android, or `gatttool`/`bluetoothctl` on Linux) to write a test payload to the characteristic and confirm the popup + vibration fire, **before** touching Gadgetbridge at all. This isolates firmware bugs from Android-app bugs.

LVGL references for any further UI work: [widget index](https://lvgl.io/docs/open/widgets), [msgbox](https://lvgl.io/docs/open/widgets/msgbox), [common widget features](https://lvgl.io/docs/open/common-widget-features) (events, styles, layouts).

## Phase 2 — Gadgetbridge: fork and build

1. Fork `Freeyourgadget/Gadgetbridge`, clone, open in Android Studio, confirm a stock build installs and runs against your phone (sanity check the toolchain before adding code).
2. Gadgetbridge is AGPLv3 — any distributed modified build must keep source available under the same license. Fine for personal/sideloaded use; flag this if you ever consider upstreaming or distributing an APK.

## Phase 3 — Gadgetbridge: DeviceCoordinator

New file, e.g. `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatchultra/TWatchUltraCoordinator.java`, extending `AbstractBLEDeviceCoordinator` (mirror `AsteroidOSDeviceCoordinator`):

- `createBLEScanFilters()` — filter on your custom service UUID from the wire protocol above.
- `supports(GBDeviceCandidate candidate)` — return true if the candidate advertises that service UUID (or matches the BLE device name you set in `hw_enable_ble`).
- `getDeviceSupportClass()` — return your `DeviceSupport` class (Phase 4).
- `supportsNotifications()` — true. Leave `supportsWeather()`, `supportsMusicInfo()`, `supportsFindDevice()`, etc. as false for v1.
- Register the new type: add an entry to the `DeviceType` enum (`app/src/main/java/nodomain/freeyourgadget/gadgetbridge/model/DeviceType.java`), e.g. `T_WATCH_ULTRA(TWatchUltraCoordinator.class)` — this is how `DeviceHelper` discovers it during device scan/pairing.

## Phase 4 — Gadgetbridge: DeviceSupport

New file, e.g. `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/twatchultra/TWatchUltraDeviceSupport.java`, extending `AbstractBTLEDeviceSupport` (mirror `AsteroidOSDeviceSupport`):

- Constructor: `addSupportedService(SERVICE_UUID)`.
- `initializeDevice(TransactionBuilder builder)`: mark device `INITIALIZING` → `INITIALIZED` (see `SetDeviceStateAction` usage in the AsteroidOS reference). No auth/pairing handshake needed for v1 — keep it minimal.
- `onNotification(NotificationSpec notificationSpec)`: build the `appName\x1Ftitle\x1Fbody` string from `notificationSpec.sourceAppName` / `.title` / `.body`, write it to the characteristic via `safeWriteToCharacteristic(builder, CHAR_UUID, bytes)`, queue it. This is the one method that actually matters for v1 — everything else can be a no-op override.
- Leave `onSetTime`, `onSetCallState`, `onSetMusicState`, etc. as empty overrides (required by the abstract base class, but you don't need to implement their bodies).

## Phase 5 — Pair and test end to end

1. Build and install the modified Gadgetbridge onto your phone.
2. Flash the updated firmware, enable BLE from the watch's BLE screen.
3. In Gadgetbridge, scan for devices — the watch should show up once `createBLEScanFilters()` matches its advertisement.
4. Pair, then trigger a real notification (e.g. a text message) and confirm it reaches `onNotification()` → BLE write → watch popup + vibration.
5. Test edge cases: long notification text (MTU truncation), rapid consecutive notifications (queue/backpressure), Bluetooth toggled off/on (reconnect), phone screen off for an extended period (Gadgetbridge's existing background/foreground-service handling should cover this — that's the whole point of building on it instead of a fresh app).

## Stretch goals (after v1 works)

- Notification dismiss sync (`onDeleteNotification`) so clearing on the phone clears the watch.
- Per-app filtering (Gadgetbridge already has this UI for other devices — check `NotificationFilter` — should mostly come for free once your `DeviceCoordinator` reports the capability).
- Battery level reporting back to the phone (`BatteryInfoProfile`, as used by AsteroidOS).
- Call state / music control, if the LilyGoLib "walkie" / audio examples in `src/factory/` end up relevant.

## Open risks

- BLE background reliability is Gadgetbridge's problem to have already solved on Android, but the **watch-side** BLE stack (NimBLE-Arduino on ESP32-S3) still needs its own reconnect-on-disconnect handling — not automatic (re-start advertising in the server's `onDisconnect` callback).
- **Two BLE stacks in one firmware**: `lib_deps` pulls both NimBLE and Bluedroid-based libraries (`ESP32 BLE Arduino`, the `ESP32-BLE-Keyboard/Mouse` forks used by the "BLE Keyboard" app). Only one Bluetooth host stack can run at a time on the ESP32-S3. Verify which stack the lewisxhe keyboard fork actually uses; at minimum, treat the notification server and the BLE-keyboard app as mutually exclusive at runtime (the existing UI already enables them from separate screens, which helps).
- No upstream PR is assumed here; this is a personal fork. If upstreaming to Gadgetbridge later, expect requests for device icons, string resources, and broader capability support beyond notifications.
