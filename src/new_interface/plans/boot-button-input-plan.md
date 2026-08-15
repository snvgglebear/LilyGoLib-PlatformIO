# Plan: BOOT button (GPIO0) as a runtime input

Today the BOOT button on T-Watch Ultra is only a hardware wake-from-sleep
source (`WAKEUP_SRC_BOOT_BUTTON` in LilyGoLib, and a busy-wait poll in the
`NO_ENTER_LIGHT_SLEEP` fallback) — nothing in `src/new_interface` reads it
while the watch is awake. This plan adds a real short-press / long-press input
on it: short press = go home / dismiss the current notification toast, long
press = flashlight. Not part of `src/custom_interface/plan.md`'s original
feature list; came out of a side discussion about the button being otherwise
idle at runtime.

**Target:** `src/new_interface`, `twatch_ultra` hardware env and
`emulator_watch_ultra`. Files touched or added, see §5.

---

## 1. What already works / exists today (verified against this repo, 2026-08-15)

### 1.1 GPIO0 is safe to poll after boot, and this codebase already touches it once

`ui_main.cpp:411-414`, inside `#ifdef NO_ENTER_LIGHT_SLEEP`:

```cpp
pinMode(0, INPUT_PULLUP);
while (digitalRead(0) == HIGH) {
    delay(10);
}
```

Active-low, `INPUT_PULLUP`, no external hardware needed — this is a blocking
wait-for-wake loop, not an event/callback mechanism, and it only runs with the
display already off. It doesn't conflict with polling GPIO0 elsewhere while
awake, but confirms the pin read itself needs no new board wiring.

The only other GPIO0/BOOT-button touch point is LilyGoLib itself
(`.pio/libdeps/twatch_ultra/LilyGoLib/src/LilyGoWatchUltra.cpp:709-729,850`,
`checkWakeupPins()`), which configures `esp_sleep_enable_ext1_wakeup_io()` on
it before `sleep()`/`lightSleep()` — again, asleep-only, no runtime conflict.

### 1.2 No general-purpose "second button" abstraction exists to extend

`hw_set_button_callback()` (`hal_interface.h:1624`, defined
`hal_interface.cpp:2622-2631`) looks like a fit but isn't: it's compiled only
under `#if defined(ARDUINO) && defined(USING_TRACKBALL)`
(`hal_interface.cpp:2585`), and wires the T-LoRa-Pager's trackball click —
undefined on `twatch_ultra`. This needs its own path, not reuse of that hook.

The PMU side (power/side) button already has an intentional, currently-empty
hook for this kind of thing — `hal_interface.cpp:951-957`:

```cpp
// Power-button events from the PMU. Currently only logged -- the hook is here
// for apps that want to intercept the side button.
instance.onEvent([](DeviceEvent_t event, void *params, void *user_data) {
    if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
        log_d("ON EVENT PMU CLICK");
    }
}, POWER_EVENT, NULL);
```

That's a different physical button (PMU-managed side key, event-driven via
`instance.onEvent`) from BOOT (bare GPIO0, no LilyGoLib event API, must be
polled). Mentioned here only so the two aren't confused later — this plan
does not touch the PMU hook.

### 1.3 No emulator input path exists for a discrete button

`main.cpp:74-77` registers exactly three LVGL/SDL input devices —
`lv_sdl_mouse_create()` (touch), `lv_sdl_mousewheel_create()` (rotary
encoder), `lv_sdl_keyboard_create()` (physical keyboard passthrough to LVGL
focus navigation) — and `hal_loop()` (`main.cpp:80-92`) does no raw SDL event
polling of its own; LVGL's SDL driver owns the event loop internally. There is
no fourth device and no spare keycode wired to anything button-shaped. Per
`src/custom_interface/plan.md`'s "make as much as possible runnable/testable
in the simulator," this plan adds one (§4) rather than making the feature
hardware-only.

### 1.4 The two actions this plan binds to already have working machinery, but not exported

- **Go home**: `menu_show()` (`ui_define.h:167`, defined `ui_main.cpp:141-148`)
  already does exactly this — sets focus back to the launcher group, slides
  `main_screen` to tile (0,0), resumes `disp_timer`, resets the idle counter,
  fires haptic feedback. `isinMenu()` (`hal_interface.h:987`, defined
  `ui_main.cpp:158-160`) reports whether the launcher is already in front, so
  the button handler can no-op instead of replaying the transition redundantly.
  No new code needed here beyond calling it.

- **Dismiss the current notification toast**: `ui_notification_popup.cpp`
  has the logic (`dismiss_toast()`, line 35-42) and the state
  (`static lv_obj_t *s_toast`, line 29) but both are file-static — nothing
  outside that file can currently ask "is a toast showing" or dismiss one.
  §5 adds `bool ui_notification_popup_is_showing(void)` and
  `void ui_notification_popup_dismiss(void)` to `ui_notification_popup.h`,
  thin wrappers around the existing private state/function.

- **Flashlight**: no dedicated feature exists, but the exact save/restore
  shape is already used for the low-power display-off path
  (`ui_main.cpp:403-406,425`): `hw_get_disp_backlight()`
  (`hal_interface.h:562`, `hal_interface.cpp:1416-1423`) to save,
  `hw_set_disp_backlight(255)` (`hal_interface.h:555`,
  `hal_interface.cpp:1409-1414`) to go full brightness, same setter to
  restore. On the emulator, `hw_get_disp_backlight()` stubs to `100` and
  `hw_set_disp_backlight()` is a no-op (no real backlight to drive) — the
  torch state itself (an on-screen white fill, see §3.3) still needs to work
  there so the feature is testable without hardware.

---

## 2. Pin read + debounce/long-press state machine

New file-local state in whichever module owns this (§5 puts it in a new
`ui_boot_button.cpp`, not `hal_interface.cpp`, to keep board-pin-polling out
of the HAL and next to the two other small always-on UI behaviors like
`ui_notification_popup.cpp`).

```cpp
#define BOOT_BUTTON_DEBOUNCE_MS   30
#define BOOT_BUTTON_LONG_PRESS_MS 500

enum BootButtonState { BB_IDLE, BB_DEBOUNCE, BB_PRESSED };
static BootButtonState s_state = BB_IDLE;
static uint32_t s_state_entered_ms = 0;
```

Polled once per `loop()`/`hal_loop()` tick (see §4 for where, since the
Arduino and emulator entry points differ):

```
BB_IDLE      --(digitalRead(0) == LOW)--------------------> BB_DEBOUNCE (record t0)
BB_DEBOUNCE  --(still LOW, millis()-t0 >= DEBOUNCE_MS)-----> BB_PRESSED (record t0 = press-confirmed time)
BB_DEBOUNCE  --(reads HIGH before DEBOUNCE_MS)-------------> BB_IDLE (bounce, ignore)
BB_PRESSED   --(reads HIGH, held < LONG_PRESS_MS)----------> fire SHORT_PRESS, -> BB_IDLE
BB_PRESSED   --(reads HIGH, held >= LONG_PRESS_MS)----------> fire LONG_PRESS,  -> BB_IDLE
```

Same shape as the wrist-tilt settle timer in
`src/custom_interface/screen_state/screen_state.cpp:90-100` and the
`WRIST_TILT_GESTURE` debounce discussion in
`src/custom_interface/plans/wrist-raise-detection.md` §4 — this repo already
has a house style for "threshold + dwell time, resolved as a flag checked
once per loop," so this reuses it rather than inventing a timer/ISR-based
approach. Polling, not `attachInterrupt()`: the existing `getTouched()`/power
button/wrist-tilt checks are all polled from `loop()` already, and GPIO0 has
no debounce hardware, so an ISR would still need software debounce on top —
polling is simpler and consistent with the rest of this codebase's input
handling.

**Long-press-while-torch-active edge case**: while the torch is on
(§3.3), holding define what counts as "released" carefully — `BB_PRESSED`'s
exit is unconditional on `digitalRead(0) == HIGH`, so releasing after a long
press correctly fires exactly one `LONG_PRESS` event, not a stream of them.
No repeat-fire while held; that's deliberate (a torch that flickered every
`LONG_PRESS_MS` while held down would be wrong).

---

## 3. Action mapping

### 3.1 Short press: go home, or dismiss a toast if one's showing

```cpp
void boot_button_on_short_press(void)
{
    if (ui_notification_popup_is_showing()) {
        ui_notification_popup_dismiss();
    } else if (!isinMenu()) {
        menu_show();
    }
    // else: already home, no toast up -- nothing to do, deliberately no-op
    // rather than re-running menu_show()'s transition/haptic for no visible change.
}
```

Toast-dismiss takes priority over navigation: a toast is drawn on
`lv_layer_top()` (`ui_notification_popup.cpp` header comment) above whatever
screen is open, so it's the thing actually in front of the user's attention
regardless of which tile `main_screen` is on.

### 3.2 Long press: flashlight toggle, not hold-to-light

Toggle (press once to turn on, press again to turn off) rather than
hold-to-illuminate: `BB_PRESSED`'s "held" duration is only known at release
time in this state machine (§2), so the earliest a hold-to-light approach
could turn the screen on is *after* the button is released, which defeats the
purpose. Toggle-on-long-press-release avoids that and matches how the
existing low-power save/restore code (§1.4) already treats brightness as a
before/after pair rather than something driven continuously per-frame.

### 3.3 Torch implementation

```cpp
static bool s_torch_active = false;
static uint8_t s_saved_brightness = 0;
static lv_obj_t *s_torch_overlay = NULL;   // full-white lv_obj_t on lv_layer_top()

void boot_button_on_long_press(void)
{
    if (!s_torch_active) {
        s_saved_brightness = hw_get_disp_backlight();
        hw_set_disp_backlight(255);
        s_torch_overlay = /* full-screen white lv_obj_t, LV_OBJ_FLAG_CLICKABLE
                              cleared so it doesn't eat touch input elsewhere */;
        s_torch_active = true;
    } else {
        hw_set_disp_backlight(s_saved_brightness);
        lv_obj_del(s_torch_overlay);
        s_torch_overlay = NULL;
        s_torch_active = false;
    }
}
```

The overlay object (not just cranking backlight) is what makes this work on
the emulator too, per §1.4's note that `hw_set_disp_backlight()` is a no-op
there — a full-white `lv_obj_t` is real, visible LVGL state regardless of
platform, and is the actual "torch" effect on hardware as well (max backlight
alone still shows whatever app/home screen was underneath, not a bright white
area to point at something).

**Interaction with sleep**: the torch must block the idle-sleep timer while
active, the same way `menu_hidden()` pauses `disp_timer` while an app is open
(`ui_main.cpp:150-153`) — otherwise the screen blanks mid-use of a flashlight,
defeating the point. `lv_timer_pause(disp_timer)` on activate,
`lv_timer_resume(disp_timer)` + `lv_disp_trig_activity(NULL)` on deactivate,
mirroring `menu_show()`/`menu_hidden()`'s existing pair exactly.

---

## 4. Wiring into the two entry points

GPIO0 read differs by platform; the debounce state machine and action
handlers (§2-3) do not, so only the poll call is platform-gated.

- **Hardware** (`new_interface.ino`, `#ifdef ARDUINO`): call
  `pinMode(0, INPUT_PULLUP)` once from a new `ui_boot_button_init()`, and
  `ui_boot_button_poll()` once per `loop()` iteration, real `digitalRead(0)`.

- **Emulator** (`main.cpp`, `#else` branch): no GPIO0 to read. Map a spare
  keycode through the existing `lv_sdl_keyboard_create()` device instead of
  adding a raw `SDL_KEYDOWN` handler — simplest way to get a key-down/key-up
  edge is a global bool flag set from an `lv_indev` key-event callback
  registered on that same keyboard indev, checked in place of `digitalRead(0)`
  inside `ui_boot_button_poll()`. Suggest a key not already claimed by LVGL's
  default navigation bindings (arrows/enter/esc/tab) — e.g. `SDLK_b` — so it
  doesn't fight normal emulator keyboard nav. This needs picking an actual
  free keycode by checking LVGL's SDL keyboard driver defaults before
  implementation; flagged as open in §6, not resolved here.

`ui_boot_button_poll()` is called from both `new_interface.ino`'s `loop()`
and `main.cpp`'s `hal_loop()` (`main.cpp:80-92`), same split every other
per-tick check in this codebase already uses (`hw_low_power_loop()`,
`app_gb_poll()`, etc).

---

## 5. New / changed surface

- **New:** `src/new_interface/ui_boot_button.h` / `.cpp` — debounce state
  machine (§2), action handlers (§3), `ui_boot_button_init()` /
  `ui_boot_button_poll()`.
- **Changed:** `src/new_interface/ui_notification_popup.h` — add
  `bool ui_notification_popup_is_showing(void)` and
  `void ui_notification_popup_dismiss(void)` declarations.
- **Changed:** `src/new_interface/ui_notification_popup.cpp` — implement the
  two new functions as thin wrappers around the existing private `s_toast` /
  `dismiss_toast()`.
- **Changed:** `src/new_interface/new_interface.ino` — call
  `ui_boot_button_init()` near the other `_init()` calls in `setup()`, and
  `ui_boot_button_poll()` in `loop()`.
- **Changed:** `src/new_interface/main.cpp` — same two calls in the native
  `setup()`/`hal_loop()`, plus the SDL keycode indev callback from §4.
- **Changed:** `src/new_interface/app_config.h` — add a
  "Boot button" section with `BOOT_BUTTON_DEBOUNCE_MS` and
  `BOOT_BUTTON_LONG_PRESS_MS` (currently sketched as plain `#define`s in §2;
  moving them here keeps every tunable constant in the one place the rest of
  this file already promises, per `src/custom_interface/plan.md`'s "all
  constants ... single location").

No changes to `hal_interface.*`, no new persisted NVS field — see §6 for why
the action mapping is deliberately not made phone/settings-configurable in
this pass.

---

## 6. Open questions / deliberately out of scope

- **Emulator keycode choice**: §4 suggests `SDLK_b` but this needs checking
  against LVGL's `lv_sdl_keyboard_create()` default key mapping (arrow
  keys/enter/escape/backspace are almost certainly claimed for focus
  navigation; letter keys likely aren't, but should be verified against the
  actual driver source in `.pio/libdeps/emulator_watch_ultra/lvgl/src/drivers/sdl/`
  before implementation, not assumed).
- **Action mapping is hardcoded, not user-configurable.** `ui_sys.cpp`'s
  settings screen and the phone-sync protocol
  (`watch-settings-sync-protocol-plan.md`) could eventually expose "what does
  BOOT do" as a setting (e.g. torch vs. some other action), but that's a
  meaningful scope increase (new persisted field, new settings row, new sync
  field) for a button whose behavior isn't validated yet. Recommend shipping
  the fixed mapping first and revisiting only if it turns out to need to be
  changeable.
- **T-LoRa-Pager / T-Watch-S3 applicability**: this plan is written against
  `twatch_ultra` (per the original discussion), but GPIO0-as-BOOT-button is
  an ESP32-S3-wide convention, not Ultra-specific — `checkWakeupPins()`
  equivalents likely exist for the other two boards too. Not verified here;
  worth a quick check of the other two `LilyGo*.cpp` variant sources before
  assuming this ports unchanged.
- **Double-press** (e.g. rapid-fire home-home for some other action) was
  considered and dropped: adds a third debounce tier for comparatively little
  payoff over separate short/long, and would add UI-discoverability cost this
  simple two-action mapping doesn't need.

---

## 7. Order of work

1. Export the two new `ui_notification_popup.h` functions (§5) — small,
   isolated, unblocks the short-press handler.
2. Build `ui_boot_button.{h,cpp}` against the hardware path only
   (`#ifdef ARDUINO`), test on real `twatch_ultra` hardware: verify debounce
   doesn't false-trigger, verify long-press threshold feels right, verify
   torch save/restore doesn't leave brightness wrong after toggling twice.
3. Add the emulator branch (§4) once a real keycode is confirmed free;
   verify short/long press both work from the SDL window.
4. Wire `disp_timer` pause/resume for the torch (§3.3) last, once the rest is
   confirmed working — easy to test in isolation by watching whether the
   screen blanks while the torch is held on past `SCREEN_SLEEP_TIMEOUT_MS`.

## 8. Test matrix

| Scenario | Expected |
|---|---|
| Short press while on home screen, no toast | No-op (already home) |
| Short press while an app is open, no toast | Returns to home (`menu_show()`) |
| Short press while a toast is showing (any screen) | Toast dismissed, screen unchanged otherwise |
| Long press (hold ≥500ms, release) | Torch turns on (full-white overlay, max backlight) |
| Long press again while torch on | Torch turns off, brightness restored to pre-torch value |
| Press held past long-press threshold, screen idle timeout would normally fire | Screen stays awake while torch is active |
| Rapid bounce on physical press (hardware only) | Debounce absorbs it — exactly one action fires, not several |
| Emulator: mapped key down/up | Same short/long behavior as hardware `digitalRead(0)` |
