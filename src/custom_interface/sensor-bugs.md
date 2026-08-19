# Bugs found while debugging wrist-raise detection

Investigation log for `screen_state.cpp`'s raise-to-wake work (see
`plans/wrist-raise-detection.md`). Two of these are upstream defects in
SensorLib that silently disable an entire class of sensor on this platform;
the rest are application bugs in our own code.

Versions this was found against: SensorLib 0.3.4 (`lewisxhe/SensorLib @ ^0.3.3`),
`platform = espressif32@6.10.0`, toolchain `xtensa-esp32s3-elf-g++ 8.4.0`,
board `twatch_ultra` (BHI260AP).

---

## 1. SensorLib: `call()` bounds its scan with the payload length

**The important one.** No gesture or event virtual sensor can ever deliver a
callback on this toolchain.

`BoschParseCallbackManager` keeps registered callbacks in a hand-rolled vector
with a member `size` holding the entry count (`BoschParseCallbackManager.hpp:57`).
Its dispatch method takes a parameter *also* named `size` — the FIFO frame's
payload length — which shadows that member:

```cpp
// BoschParseCallbackManager.hpp:148
void call(uint8_t sensor_id, uint8_t *data, uint32_t size, uint64_t *timestamp)
{
#ifdef USE_CUSTOM_VECTOR
    for (uint32_t i = 0; i < size; i++) {     // <-- payload length, not entry count
        if (entries[i].cb) {
            if (entries[i].id == sensor_id) {
                entries[i].cb(sensor_id, data, size, timestamp, entries[i].user_data);
```

So the number of registered callbacks examined equals the number of *bytes* in
the arriving frame.

### Why it only bites here

The buggy branch is selected by compiler version:

```cpp
// BoschParseCallbackManager.hpp:35
#if __GNUC__ < 10
#define USE_CUSTOM_VECTOR
#else
#include <vector>
#endif
```

The `#else` path iterates `entries.size()` and is correct. ESP32 Arduino ships
GCC **8.4.0**, so `__GNUC__ == 8` and the broken branch is always what compiles
for these boards. Anyone on a newer toolchain never sees this.

### Why it kills gestures specifically

The bound is `dev->event_size[sensor_id]`. Measured on this board:

| sensor | id | `event_size` | entries scanned |
|---|---|---|---|
| Accelerometer passthrough | 1 | **7** | `entries[0..6]` |
| Wake / Glance / Pickup / Wrist-tilt gesture | 57 / 59 / 61 / 67 | **1** | `entries[0]` only |
| Tilt / Step detector, Significant motion | 48 / 50 / 55 | **1** | `entries[0]` only |

Event sensors carry a one-byte payload, so exactly one entry is checked.
`entries[0]` is whatever registered first — in our case the accelerometer, via
`accelSensor().enable()`. A gesture frame therefore compares against id 1,
fails to match, and is dropped. The callback is never invoked regardless of
sample rate, FIFO settings, or registration order beyond slot 0.

Streaming sensors work by luck: a 7-byte accel frame scans `entries[0..6]`,
which happens to cover both the accelerometer and the gravity vector.

### The symptom this produces

Everything reports success and nothing ever fires:

- `configure()` returns true, and `getConfigure()` reads back `rate:1.00Hz`
  straight off the chip — the sensor really is enabled.
- `onResultEvent()` returns true (it validates against
  `bhy2_is_sensor_available()` first).
- Host interrupt control shows both wake-up and non-wake-up FIFOs enabled.
- The FIFO is demonstrably being pumped, because accel data flows at 25 Hz
  through the same path.

There is no error anywhere. SensorLib's own diagnostics go through `log_e()`,
which `CORE_DEBUG_LEVEL=0` silences in this project.

### Same defect, second and worse consequence: out-of-bounds call

The storage behind those entries is raw uninitialised memory — `malloc()` in the
constructor, `realloc()` in `expand()`, so `Entry`'s constructor never runs on
any slot:

```cpp
// BoschParseCallbackManager.hpp:76
size = 0;
capacity = 10;
entries = static_cast<Entry *>(std::malloc(capacity * sizeof(Entry)));
```

Because the scan is bounded by payload length rather than entry count, a frame
larger than the number of registered callbacks walks past the initialised
entries into heap garbage. It then tests `entries[i].cb` (garbage, usually
passes the NULL check) and `entries[i].id == sensor_id` (garbage, ~1 in 256 per
slot) and on a match **calls a junk function pointer**.

A `GRAVITY_VECTOR` frame is 7 bytes, so with fewer than 7 registered callbacks
this reads 6 slots of garbage 25 times a second. Observed as an immediate boot
loop: `LoadProhibited`, `EXCVADDR: 0x0000000b`, backtrace landing in
`BoschParseStatic::parseMetaEvent` — a stale-looking callback invoked with a
nonsense reference.

This is latent whenever enough sensors happen to be subscribed to keep the scan
in bounds, and it surfaces the moment that count drops. Padding the array with
extra subscriptions hides it rather than fixing it; the only safe course on this
toolchain is not to enter the manager at all.

### Upstream fix

Rename the parameter, or qualify the member:

```cpp
for (uint32_t i = 0; i < this->size; i++) {
```

### Our workaround

See `hookFifoId()` in `screen_state/screen_state.cpp` — bypass the manager and
take delivery straight from bhy2's FIFO parse table. Note this depends on bug 2.

---

## 2. SensorLib / bhy2: FIFO callbacks append but dispatch reads only the first

`bhy2_register_fifo_parse_callback()` writes into the first **free** slot and
never replaces an existing registration for the same id:

```cpp
// bhy2.c:1265
for (i = 0; i < BHY2_MAX_SIMUL_SENSORS; i++) {
    if (dev->table[i].sensor_id == 0) {     // only ever fills empty slots
        dev->table[i].sensor_id = sensor_id;
        dev->table[i].callback = callback;
        ...
        break;
    }
}
```

Dispatch, however, resolves through `get_callback_info()`, which returns a
single entry — the first match (`bhy2.c:1713`, invoked at `bhy2.c:1732`).

Together these mean **an id can be registered twice, return `BHY2_OK` both
times, and the second registration is permanently dead.**
`SensorBHI260AP::initImpl()` (`SensorBHI260AP.cpp:1187`) claims every available
id for `parseData` at `begin()`, so any later attempt to hook a sensor directly
silently does nothing.

Secondary effect: the table is `BHY2_MAX_SIMUL_SENSORS == 48`
(`bhy2_defs.h:584`) and `initImpl()` consumes ~38 of those, so dead duplicate
registrations exhaust it quickly. Registering a catch-all across the id space
succeeded only 4 times before returning
`BHy2_E_INSUFFICIENT_MAX_SIMUL_SENSORS`.

**Working around it** requires overwriting the slot in place rather than calling
the registration API — matching on `sensor_id` and replacing `callback` /
`callback_ref`. `SensorBHI260AP::getHandler()` exposes the `bhy2_dev*` needed.

---

## 3. `ORIENTATION` is absent from this board's fusion firmware

Not a code bug, but it costs a flash cycle to discover, and `enable()` failing
is the only signal.

The BHI260AP's available virtual sensors depend on the firmware blob
`LilyGoUltra::initSensor()` loads. On T-Watch-Ultra, **`ORIENTATION` (id 43) is
not present** — `SensorEuler::enable()` returns false and every reading stays
pinned at `0.00`.

Present and usable instead:

| id | sensor | note |
|---|---|---|
| 1 | Accelerometer passthrough | gravity **+** linear acceleration, in g |
| 28 | **Gravity vector** | fused; linear acceleration removed, unit-normalised, also in g |
| 31 | Linear acceleration | the complement |
| 37 | Game rotation vector | quaternion, accel+gyro, no euler singularities |
| 48 / 50 / 55 | Tilt detector / Step detector / Significant motion | event sensors |
| 57 / 59 / 61 / 67 | Wake / Glance / Pickup / Wrist-tilt gesture | event sensors |

`GRAVITY_VECTOR` is the drop-in replacement for raw accel: it parses through
`bhy2_parse_xyz` and scales at `1/4096`, so it is the same `SensorXYZ` type in
the same units, and thresholds transfer between the two while held still.

To re-dump the list: `instance.sensor.getSensorInfo().printInfo(Serial)`.

---

## 4. `SensorXYZ` cannot read `ORIENTATION` — it silently reads the wrong union member

Swapping the id on a `SensorXYZ` does **not** switch it to euler angles.
`BoschSensorDataHelper.hpp:115` routes `BHY2_SENSOR_ID_ORI` to
`bhy2_parse_orientation()`, which fills `result.orientation`, but
`SensorXYZ::updateValue()` reads `data.vector`. The result is garbage, not a
compile error. Euler angles need the separate `SensorEuler` class
(`BoschSensorDataHelper.hpp:363`), which has `getHeading()/getPitch()/getRoll()`
and bakes the id in.

---

## 5. Application bugs in `screen_state.cpp`

Each of these independently produced "nothing happens", and they stacked.

### 5a. The virtual sensor was never enabled

`screen_state_init()`'s `#ifdef HAS_WRIST_TILT_SENSOR` block was empty. `enable()`
is what registers the callback *and* configures the sensor:

```cpp
// BoschSensorDataHelper.hpp:189
bool enable(float sample_rate, uint32_t report_latency_ms) {
    _handle.onResultEvent(_sensor_id, staticCallback, this);
    return configure(sample_rate, report_latency_ms);
}
```

Without it `ACCEL_PASSTHROUGH` never reports, `hasUpdated()` is permanently
false, and every `Serial.printf` behind that gate is unreachable. For BHY2
virtual sensors a rate of 0 Hz means *disabled* — a gesture sensor still needs a
nonzero rate to report at all.

### 5b. The sensor object must be constructed after `instance.begin()`

`BoschSensorDataHelperBase`'s constructor caches the scale factor
(`BoschSensorDataHelper.hpp:53`):

```cpp
_scaling_factor = _handle.getScaling(_sensor_id);
```

A file-scope `SensorXYZ` would run before the BHI260AP firmware has booted and
latch a bogus scale, making `getX/getY/getZ` return `0.0` forever. Hence the
lazy function-local-static accessors (`accelSensor()`, `gravitySensor()`),
first called from `screen_state_init()`.

### 5c. Threshold constants were in the wrong unit

`ACCEL_PASSTHROUGH` scales by `1/4096` (`common.cpp`, `get_sensor_default_scaling`),
so readings are **g** — gravity ≈ 1.0, saturating at ±8. The `LOOKING_*`
constants were degree-scale values (`-95`, `48`), which are unreachable:
`accel.getZ() > 48` can never be true. Degree-shaped numbers most likely came
from an orientation readout, whose scaling is `360/32768`.

### 5d. Misplaced parenthesis folded a comparison inside `abs()`

```cpp
abs(accel.getZ() + LOOKING_Z<OFFSET)     // "< OFFSET" is INSIDE abs()
```

This evaluates `(z + 48) < 25` to a `bool`, then takes `abs()` of it, yielding
`0` or `1` — nothing is ever compared against `OFFSET`. It is `1` only when
`z < -23`, unreachable in g, so the term was permanently `0` and `&&` made the
whole condition permanently false regardless of the other axes.

Note `abs` here is `std::abs` (`Arduino.h:201`, `using std::abs;`), not the
classic Arduino macro, so float overloads resolve correctly — truncation was
never the issue.

### 5e. Sign inverted the target

`abs(v + LOOKING_V) < OFFSET` tests whether `v` is near **minus** the target.
With `LOOKING_Y -95` it was looking for `y ≈ +95`. Testing "within OFFSET of the
target" requires subtraction: `abs(v - LOOKING_V) < OFFSET`.

---

## 6. Build/flash issues hit along the way

Recorded here because both cost a debugging cycle; the fixes are committed with
explanatory comments in `platformio.ini`.

- **`main.ino` + `main.cpp` in the same directory collide.** PlatformIO converts
  `<name>.ino` to a `.cpp` of the same basename, so both mapped to object
  `src/main.cpp.o`. The emulator-only `main.cpp` (entirely inside
  `#ifndef ARDUINO`) won, compiling to an object with zero symbols, and the
  `.ino` was never built — producing `undefined reference to 'setup()'` only
  once a full rebuild cleared the stale object. Every other app in this repo
  avoids it with a distinct `.ino` basename; `main.ino` is now
  `custom_interface.ino`.
- **Upload failing on native USB-CDC.** The ESP32-S3 enumerates as USB-JTAG
  (`303A:1001`), where baud is virtual. Both the `921600` renegotiation and the
  esptool stub re-enumerate the CDC device, which the devcontainer's USB
  passthrough does not follow — esptool reports `No serial data received` after
  correctly identifying the chip. Fixed with `upload_speed = 115200` and
  `upload_flags = --no-stub`.

---

### 5f. Diagnostic latched on first occurrence and made a working sensor look broken

Worth recording because it produced a confidently wrong conclusion. The first
version of the FIFO probe kept a per-id `reported[]` flag and printed only the
*first* frame each id ever delivered. Once bugs 1 and 2 were worked around the
event sensors began firing normally, but the log still showed a handful of
one-off lines — which reads as "fires occasionally, no pattern" rather than
"fires reliably, reported once". Replaced with a ring buffer that logs every
event, its running count per id, and the gravity vector at that instant.

---

## Confirmed once delivery was fixed

With the table-overwrite workaround in place and the watch actually worn, five
event sensors deliver: `SIG_MOTION` (55), `TILT_DET` (48), `PICKUP` (61),
`STEP_DET` (50) and `WRIST_TILT` (67). `WAKE` (57) and `GLANCE` (59) were not
observed. This confirms bug 1 as the sole reason for the earlier total silence.

## Calibration result

124 logged samples of worn use separate cleanly on the **gravity vector's Z
axis**, with no threshold tuning needed:

| state | n | gravity x (mean) | gravity z |
|---|---|---|---|
| raised / looking | 24 | +0.26 | +0.74 … +0.98 (mean +0.89) |
| arm down | 90 | +0.89 | −0.69 … +0.30 (mean +0.00) |
| ambiguous | 10 (8.1%) | +0.66 | +0.33 … +0.67 |

A single-axis test on `GRAVITY_VECTOR` — raised when `z > 0.7`, released when
`z < 0.4`, with the ~200 ms settle from the plan's §4 — is both simpler and
better separated than the three-axis `LOOKING_X/Y/Z` ± `OFFSET` window, which
never worked in any unit. X is the complement (≈0.9 down, ≈0.26 raised) and can
serve as a confirmation term but is not needed.

## Outcome

`screen_state.cpp` now detects a raise from `GRAVITY_VECTOR`'s Z axis with a
three-state machine (`WRIST_DOWN` → `WRIST_SETTLING` → `WRIST_RAISED`), a 200 ms
settle to reject swing-throughs, and hysteresis between `WRIST_RAISED_Z` (0.70)
and `WRIST_RELEASED_Z` (0.40). `wrist_tilt_detected` is a one-shot edge consumed
by `manageSleepState()` rather than a level, so a raised wrist no longer holds
the idle timer open indefinitely.

The gesture route was dropped. It cost two library bugs to reach, and even once
delivery worked the edge polarity of each algorithm remained unknown, while the
gravity threshold was measurable, well separated and needed no black box.

All `WRIST_CALIBRATION_LOG` scaffolding is removed: the seven event-sensor
subscriptions, the FIFO table overwrite, the ring buffer, the config/interrupt
dumps and the 1 Hz dual-stream log. Boot output is silent again, and the only
sensor the shipping firmware enables is `GRAVITY_VECTOR` at 25 Hz.

## Status

Bugs 1 and 2 are worked around permanently in `screen_state.cpp`: the gravity
vector is taken straight from bhy2's parse table via `hookFifoId()` and decoded
with `bhy2_parse_xyz()`, so `BoschParseCallbackManager` is never entered. This
is not optional tidiness — with only one subscription the manager's
out-of-bounds call crashes the firmware in seconds.

`SensorXYZ`, `SensorEuler` and any other `BoschSensorDataHelper` type are unsafe
on this toolchain unless at least `event_size` callbacks happen to be
registered. Anything added later that wants a BHI260AP sensor should follow the
same direct-registration pattern.

Bugs 5a–5f are fixed.

All diagnostic scaffolding sits inside the `WRIST_CALIBRATION_LOG` block in
`screen_state/screen_state.cpp` and is meant to be deleted once calibration
settles: seven event-sensor subscriptions, config readbacks, the interrupt-control
dump and the 1 Hz dual-stream log are far more sensor load than the shipping
detector needs.
