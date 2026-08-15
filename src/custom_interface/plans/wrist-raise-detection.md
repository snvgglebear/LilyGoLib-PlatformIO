# Plan: raise-to-wake from raw accelerometer

`screen_state.cpp` currently wakes the screen using the BHI260AP's built-in
`WRIST_TILT_GESTURE` virtual sensor (id 67). On hardware it fires on the wrong
edge: lowering the watch (vertical -> horizontal) wakes it, raising it
(horizontal -> vertical, the "look at watch" motion) does not.

## 1. Why the built-in gesture is the wrong thing to keep chasing

`LilyGoUltra::initSensor()` (`LilyGoWatchUltra.cpp`) calls
`sensor.setRemapAxes(SensorBHI260AP::TOP_LAYER_BOTTOM_RIGHT_CORNER)` once,
globally, before our code ever runs. `WRIST_TILT_GESTURE` is a closed firmware
algorithm baked into the Bosch fusion blob (`BOSCH_BHI260_KLIO` /
`BOSCH_BHI260_GPIO` on this board) - it has its own built-in assumption about
which physical direction is "raised to view," and that assumption is defined
relative to the chip's mounting, i.e. relative to that remap. The remap's
`TOP_LAYER_*` / `BOTTOM_LAYER_*` split encodes a Z-axis polarity choice, so a
mismatch between what LilyGo picked (presumably tuned for correct touch/display
orientation, not for this gesture) and what the algorithm expects is the
likely reason the direction is inverted.

We could try overriding the remap ourselves after `instance.begin()` to hunt
for a value that also makes the gesture algorithm agree - but that's a guess
against closed firmware, needs a flash-and-test round trip per attempt, and
risks quietly breaking something else that depends on the vendor's remap
later (there's nothing else consuming it today, but it's still global state we
don't own). Reading raw acceleration and deciding "raised" ourselves sidesteps
the guessing entirely: we pick the axis and the sign, we can log our way to the
right calibration, and the polarity is ours to define instead of Bosch's.

## 2. Data source

Same subscription mechanism already used for the gesture sensor
(`instance.sensor.onResultEvent()` / `configure()`), just pointed at a
different, always-available virtual sensor id instead of a gesture: raw
acceleration passthrough. Follows the pattern in
`.pio/libdeps/twatch_ultra/SensorLib/examples/BHI260AP_6DoF/BHI260AP_6DoF.ino`,
which wraps `ACCEL_PASSTHROUGH` in a `SensorXYZ` helper for polling instead of
a manual byte-parsing callback:

```cpp
#include <bosch/BoschSensorDataHelper.hpp>

static SensorXYZ accel(SensorBHI260AP::ACCEL_PASSTHROUGH, instance.sensor);

// in screen_state_init(), after instance.begin():
accel.enable(/*sample_rate*/ 25.0f, /*report_latency_ms*/ 0);
```

25 Hz is a starting guess - fast enough to catch a deliberate raise (a few
hundred ms) without the FIFO/interrupt overhead of the 100 Hz used in the SDK
examples for continuous motion tracking. Revisit if the detector feels laggy
or misses fast raises.

`ACCEL_PASSTHROUGH` reports in the same remapped axis frame `WRIST_TILT_GESTURE`
used, so the earlier axis-direction problem doesn't disappear on its own - we
still have to work out empirically which axis and sign mean "raised." The
difference is that here it's *our* threshold, so once found it can't drift
out from under us the way a black-box gesture's polarity did.

## 3. Calibration step (needs hardware, do this before writing the real detector)

Add a temporary diagnostic (behind a `#define WRIST_CALIBRATION_LOG` or similar,
stripped before this ships) that prints `accel.getX()/getY()/getZ()` on every
update in `manageSleepState()`, rate-limited to a few times a second so the
log stays readable:

```cpp
if (accel.hasUpdated()) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 200) {
        Serial.printf("accel x:%+6.2f y:%+6.2f z:%+6.2f\n",
                       accel.getX(), accel.getY(), accel.getZ());
        last_log = millis();
    }
}
```

Flash, open the serial monitor, and log while: (a) arm hanging naturally at
rest, (b) deliberately raising to a "checking the time" angle, (c) the false
trigger case from today (lowering flat). Compare the three to find which axis
separates "raised" from both other states cleanly, and in which direction.
Wrist-mounted watches are usually a gravity-projection problem - the axis
that's roughly aligned with the arm when hanging (near ±9.8) and rotates
toward a different axis as the wrist tilts up is the one to key on - but don't
assume, read the actual numbers, the remap makes this board-specific.

## 4. Detection state machine

Once the axis/sign is known, a single instantaneous threshold crossing will
false-trigger on arm swings (walking, gesturing) that pass through the same
angle without settling there. Use a threshold plus a short settle/dwell time,
the same shape real raise-to-wake implementations use:

```
state: RESTING -> (axis crosses threshold) -> SETTLING (start timer)
SETTLING -> (axis still past threshold after ~150-250ms) -> fire wake, -> RAISED
SETTLING -> (axis drops back below threshold before settle time) -> RESTING (no fire)
RAISED -> (axis drops back below threshold) -> RESTING
```

This wants tuning against real swing-vs-raise samples from step 3, not a
number picked in the abstract. Start with a generous settle time (~200ms) and
tighten only if raises feel slow to register.

## 5. Integration into `screen_state.cpp`

- Remove the `WRIST_TILT_GESTURE` `onResultEvent`/`configure` calls and the
  `wristTiltResultCb` callback - keep the diagnostic Serial line pattern
  (report over `Serial` directly, not `log_e`, since `CORE_DEBUG_LEVEL=0` in
  this project) for whatever replaces it.
- Add the `SensorXYZ accel` instance, the calibration constants once known,
  and the settle-time state machine, gated behind the same
  `HAS_WRIST_TILT_SENSOR` board check (still BHI260AP-only - T-Watch-S3's
  BMA423 has no equivalent raw passthrough exposed through this code path
  today).
- Drive the state machine from `manageSleepState()` alongside the existing
  touch/power-button checks, same `else if` chain, same
  `instance.wakeupDisplay()` call on a positive edge.

## 6. Open questions

- Threshold will likely need to be a per-wearing-angle compromise; if it's
  ever unreliable across users, worth surfacing as a tunable rather than a
  hardcoded constant, but not before it's known to work at all.
- 25 Hz continuous polling is more sensor traffic than the interrupt-driven
  gesture event was (which only reported on an actual detection). If battery
  impact turns out to matter, look at whether `report_latency_ms` can batch
  updates, or whether a lower sample rate still catches a deliberate raise.
