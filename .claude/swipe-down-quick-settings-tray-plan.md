# Plan: Swipe-down quick-settings tray for `src/custom_interface`

## Goal

Pull down from the top of the screen to reveal a quick-settings shade — time, date, battery,
brightness (volume/Wi-Fi deferred, see Step 1) — then swipe up, tap the scrim, or tap the grabber
to dismiss it.

Scope decisions:

- **Quick-settings shade**, not an app grid and not a notifications list.
- **`src/custom_interface` from scratch** — this is *not* a feature bolted onto `src/factory`'s
  launcher. There is no tileview, no `app_t` grid, no `hal_interface.h` wrapper layer here. The
  tray owns `lv_screen_active()` outright and talks to LilyGoLib's `instance` directly.
- **T-Watch-Ultra only, for now.** `custom_interface`'s only board-specific code today
  (`usable_area/`) is a curved-bezel engine hardcoded to the Ultra's geometry — see "Current
  state" below for why T-Watch-S3 isn't a drop-in target yet. T-LoRa-Pager has no touchscreen and
  was never in scope.

## Current state of `src/custom_interface` (verified 2026-08-14)

This directory is much earlier-stage than `src/factory` was when the original version of this
plan was written against it. Read this section before writing any tray code — most of it is
prerequisite work, not tray design.

- **`main.ino` does not build.** It has no `#ifdef ARDUINO` guard, doesn't `#include <LilyGoLib.h>`
  or `<LV_Helper.h>`, and never calls `instance.begin()` / `beginLvglHelper()`. It calls
  `safe_area_init()`, which **does not exist** — `usable_area.h` declares `safe_area_init(void)`
  but `usable_area.cpp:7` defines `useable_area_init(void)` (typo, not an overload). This is a
  guaranteed link error on the hardware build today.
- **There is no `main.cpp`.** `emulator_*` envs need a native/SDL2 entry point that calls
  `lv_init()`, creates the SDL window/indevs, then calls into the shared UI-setup code — see how
  `src/factory/main.cpp` and `src/watch_interface/main.cpp` do this. Without one, an emulator build
  of `custom_interface` has no `main()` to link against and cannot run at all.
- **No hal_interface layer.** `src/factory` reads hardware through `hw_*()` wrappers in
  `hal_interface.cpp`; `custom_interface` has nothing like that. The sibling `src/watch_interface`
  sandbox (and every LilyGoLib example) calls `instance.*` directly, `#ifdef ARDUINO`-gated by
  hand in each file that needs it. Follow that pattern here, not factory's.
- **No tileview, no launcher, no menu group.** The entire factory-plan trap list about
  `lv_tileview_get_tile_active()` returning `NULL`, gestures bubbling past `menu_panel`, and
  `exit_func_cb` being dead code — none of it applies. There is exactly one screen. Attach the
  gesture callback to `lv_screen_active()` and there is nothing to route around.
- **Fonts are not a concern here.** LilyGoLib's own `lv_conf.h` enables every Montserrat size
  10–48 unconditionally on hardware (`.pio/libdeps/twatch_ultra/LilyGoLib/src/lv_conf.h:484-504`),
  and `env_emulator`'s build flags enable the same superset natively
  (`platformio.ini:275-286`). Factory's `MAIN_FONT` macro is defined in
  `src/factory/hal_interface.h`, not by LilyGoLib — it doesn't exist here and you don't need it.
  Use any `lv_font_montserrat_*` directly.
- **The `lv_point_t`/`lv_point_precise_t` trap doesn't apply either.** It comes from
  `src/factory/ui_define.h`'s `#define lv_point_t lv_point_precise_t`, not from LVGL or
  `LV_USE_FLOAT=1` itself (which *is* set for `env_emulator`, `platformio.ini:275`). Don't
  reintroduce that macro here and `lv_indev_get_point()` behaves normally — moot anyway since the
  gesture design below never calls it.
- **`widgets/` is empty.** It reads as the intended home for extracted widget source files (this
  tray included), parallel to how `usable_area/` holds the bezel engine.
- **T-Watch-S3 is out of scope until `usable_area.cpp` is board-gated.** `BEZEL_RADIUS` (120) is
  applied to the screen root unconditionally in `useable_area_init()`. On the S3's flat 240×240
  panel that constant is roughly half the screen height, so *every* band would compute a nonzero
  bezel inset — the app would visibly break, not just render suboptimally. Don't assume the tray
  "just works" there; it needs `#if defined(ARDUINO_T_WATCH_S3_ULTRA)` around the bezel math first,
  which is separate work from this plan.
- **No sleep/idle model exists yet to hook a tray auto-close into.** `src/testing_safe_area`'s
  `safe_area.ino` is actively prototyping touch-vs-power-button wake/sleep behavior in this
  session (`git diff` shows `ManageSleepState()` was just fixed to stop a held touch from spamming
  `sleepDisplay()`/`wakeupDisplay()`), but that logic lives in a different `src_dir` and hasn't been
  ported into `custom_interface`. See Step 4.

## Step 0 — get `custom_interface` building at all

None of this is tray-specific; it's the same foundational scaffolding `src/factory` and
`src/watch_interface` already have. Do this first — everything after it assumes a working base.

1. **Fix the name mismatch.** Rename `useable_area_init` → `safe_area_init` in
   `usable_area.cpp` to match the header (or vice versa — just make them agree).
2. **Rewrite `main.ino`** to mirror `src/watch_interface/watch_interface.ino`:
   ```c
   #ifdef ARDUINO
   #include <LilyGoLib.h>
   #include <LV_Helper.h>

   static void setupGui()
   {
       safe_area_init();
       lv_obj_t *btn_area = safe_area_place(lv_screen_active(), 6, 40);
       if (btn_area) { /* existing button code */ }
   }

   void setup()
   {
       Serial.begin(115200);
       instance.begin();
       beginLvglHelper(instance);
       setupGui();
   }

   void loop()
   {
       instance.loop();
       lv_timer_handler();
       delay(5);
   }
   #endif
   ```
   Pulling the screen-building code into a plain `setupGui()` function (rather than leaving it
   inline in `setup()`) is what lets `main.cpp` call the identical code natively — same reason
   factory and watch_interface both do it.
3. **Add `main.cpp`**, copied from `src/watch_interface/main.cpp`'s native/SDL2 skeleton
   (`#ifndef ARDUINO`, `lv_init()`, `lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES)`, mouse/
   mousewheel/keyboard indevs, then call the same `setupGui()`). `safe_area_init()` only needs
   `lv_display_get_horizontal/vertical_resolution(NULL)`, so it works unchanged once the SDL window
   exists — no native stub needed for it.
4. **Verify:**
   ```bash
   pio run -e emulator_watch_ultra -t exec   # SDL window, "tap me" button visible
   pio run -e twatch_ultra -t upload          # links without the safe_area_init error
   ```

## Step 1 — a tiny HAL shim for what the tray needs

`custom_interface` has nothing like `hw_get_monitor_params()` / `hw_set_disp_backlight()`. Add a
small, tray-scoped file — e.g. `widgets/quick_settings_tray_hal.h/.cpp` — with only the 3 calls
the MVP tray needs, `#ifdef ARDUINO`/native-stub split by hand (same shape as
`hal_interface.cpp`'s split, just far smaller since there's no `hw_*` API surface to preserve):

| Data | Hardware call | Template to copy from |
|---|---|---|
| Time + date | `instance.rtc.getDateTime(&timeinfo)`, then `strftime()` | `LilyGoLib/examples/peripheral/RTC_TimeLib/RTC_TimeLib.ino` — copies straight over, it already does exactly this with no hal layer |
| Battery % + charging | `instance.pmu.getBatteryPercent()`, `instance.pmu.isCharging()` | `LilyGoLib/examples/power/PowerManageMonitor/PowerManageMonitor.ino` — note its `setup()` calls `instance.pmu.enableBattDetection()` / `enableVbusVoltageMeasure()` / `enableBattVoltageMeasure()` / `enableSystemVoltageMeasure()` once; do the same in this app's `setup()`, not per-poll |
| Brightness get/set | `instance.getBrightness()` / `instance.setBrightness(level)`, range `DEVICE_MIN_BRIGHTNESS_LEVEL`..`DEVICE_MAX_BRIGHTNESS_LEVEL` (0–255 on watch boards) | `LilyGoLib/examples/peripheral/DisplayBrightness/DisplayBrightness.ino` — wires a slider's `LV_EVENT_VALUE_CHANGED` straight to `instance.setBrightness()` already; that's most of the brightness row done |
| Native stubs | fixed/fake values | Shape only (not code) from `src/factory/hal_interface.cpp`'s `#else` branches — e.g. its `30 + rand() % 71` battery jitter (`:1905`). Don't copy factory's actual functions; they're entangled with `USING_AUDIO_CODEC`/`HW_*_ONLINE` bitmasks this app doesn't have |

**Defer Wi-Fi state and volume from the MVP.** `main.ino` never calls `WiFi.begin()` today, and
volume needs `instance.codec` initialized and online-checked first (`USING_AUDIO_CODEC`-gated in
factory, not something `custom_interface` has stood up). Ship clock + battery + brightness first;
add those two once/if this app grows real WiFi/audio bring-up rather than faking them for the tray.

Unlike factory's `hw_set_user_setting()` (which writes NVS unconditionally, forcing a dirty-flag
pattern — see the flash-write-per-pixel trap this plan originally called out), `instance.setBrightness()`
is a direct hardware call, not a persisted setting, in the examples above. Confirm whether
`instance.setBrightness()` also persists across reboot before assuming you need your own
dirty-flag/flush-on-close logic — if it doesn't persist, that's a smaller problem than factory's,
not a bigger one.

## Step 2 — object tree

Same `lv_layer_top()` scrim+tray shape as before, sized for the one board this targets:

```
lv_layer_top()
├── scrim   100%x100%, black LV_OPA_50, CLICKABLE, HIDDEN when closed → tray_close()
└── tray    100% x tray_h, y animated from -tray_h, HIDDEN when closed,
            opaque bg, flex column
    ├── header    time + date  |  battery bar + % + charging icon
    ├── brightness row  icon + slider + % label
    └── footer    grabber (LV_SYMBOL_UP) → tray_close()
```

(No Settings shortcut row yet — there's no Settings screen in `custom_interface` to jump to.
Add it back once one exists; a placeholder button that does nothing is worse than omitting it.)

**Route tray content through the safe-area engine, not a bare flex column at `y=0`.** This is the
one thing genuinely new to this version of the plan: `custom_interface` already has a working
curved-viewport layout tool, and the tray drops from the top — exactly the region
`safe_area_inset_at()` reports the largest insets for. Build the header/brightness rows as
`safe_area_place(tray, y, height)` bands stacked per `HOWTO.md`'s pattern, instead of assuming the
tray's rounded corners alone keep content off the bezel. Skip this for the scrim (a fullscreen
dimmer has nothing to clip) and for the tray's own outer container (position/animate that one in
plain screen coordinates; only its *children* need the safe-area bands).

**Group hygiene, animation approach (named `tray_anim_y_cb` wrapper, `ease_out`/`ease_in`,
completion callback hides on close, state enum not a bool), and per-board sizing table** carry over
unchanged from the original design — they're LVGL mechanics, independent of which app hosts the
tray. At 502×410 only one board size exists here, so the table collapses to one column; there's no
240×240 row until T-Watch-S3 gets bezel-gated (see "Current state").

## Step 3 — gesture handling

Attach directly to `lv_screen_active()`. There is no tileview to fight, so the original plan's
traps 2 and 3 (gesture bubbling past `menu_panel`, `lv_tileview_get_tile_active()` returning `NULL`
on a fresh boot) don't exist as problems here — there's only one screen and it's always "the
launcher." What's still real and unchanged:

- **`LV_OBJ_FLAG_GESTURE_BUBBLE`** on any clickable content the gesture must cross — today that's
  just the "tap me" button; add it to every future icon/widget the tray needs to open over.
- **`lv_indev_wait_release()`** right after `tray_open()`/`tray_close()` — otherwise the swipe that
  opens the tray still delivers `LV_EVENT_CLICKED` to whatever it started on.
- **Non-scrollable tray**, sliders consuming their own drags (LVGL suppresses gesture detection
  once a scroll object is active).
- **Gesture threshold facts**, unrelated to app structure: LVGL needs 50 px of travel *and* ≥3 px
  per input sample (a stop-start drag resets the accumulator — bites scripted/synthetic input
  hardest); an upward close-swipe starting near the top of the tray barely clears 50 px, which is
  why the grabber and scrim tap are load-bearing, not nice-to-haves.
- **No modal guard needed for MVP** — `custom_interface` has no msgbox system yet. If one gets
  added later, the same `lv_msgbox_create(NULL)` → parents to `layer_top` → gesture resolves there
  first, never reaching the screen callback trick from the factory plan applies unchanged.

**No example to copy for the gesture wiring itself.** Checked every file under
`LilyGoLib/examples/lvgl/` for `LV_EVENT_GESTURE` — zero hits. The library's own examples
(`lvgl/event/lv_example_event_*.c`) are a good template for general `lv_obj_add_event_cb()` style,
but gesture-dir handling has no upstream example; the mechanics above (and LVGL's own docs) are
the only source for it.

## Step 4 — idle / auto-close: decide scope explicitly

`custom_interface` has no `SCREEN_TIMEOUT`/low-power-flag machinery to hook into, unlike
`src/factory`. Two honest options, pick one rather than half-porting factory's version:

1. **Ship without auto-close for the MVP.** Grabber, scrim tap, and swipe-up are three independent
   close paths already — a tray that only closes on user action is a complete, shippable feature.
   Add `lv_display_trigger_activity(NULL)` in `tray_open()`/`tray_close()` regardless, so opening
   the tray doesn't itself trigger whatever screen-blanking exists.
2. **If auto-close matters now**, build the smallest version that works: a 1 Hz `lv_timer` that
   calls `lv_display_get_inactive_time(NULL)` and closes past some threshold — no "restore the
   previous low-power flag" complexity needed, because nothing else in this app sets that flag yet.
   For the touch-vs-sleep interaction specifically, `src/testing_safe_area/safe_area.ino`'s
   just-fixed `ManageSleepState()` (this session's `git diff`) is the nearest working reference in
   this codebase for "a held touch must not repeatedly toggle sleep state" — read it before writing
   your own idle loop, even though it lives in a different `src_dir`.

Recommendation: (1) first, revisit (2) once `custom_interface` grows a real sleep/idle model for
reasons beyond the tray.

## Verification

```bash
pio run -e emulator_watch_ultra -t exec    # 502x410, only board this targets
pio run -e twatch_ultra -t upload
pio run -e twatch_ultra -t monitor
```

No `tlora_pager` exclusion check is needed — this app was never built for that board, unlike
factory where the tray had to be compiled out of an existing multi-board app.

**Test matrix**, trimmed from the factory-plan version for what actually exists here:

| # | Action | Expected |
|---|---|---|
| 1 | Swipe down on the screen | Tray slides in, scrim dims the background |
| 2 | Swipe down starting **on the button** | Tray opens; button's click does **not** fire |
| 3 | Swipe up / tap scrim / tap grabber | Each closes the tray |
| 4 | Tray closed → tap the button | Still works; nothing swallowed by `layer_top` |
| 5 | Drag brightness slider | Backlight tracks live (hardware only — native `setBrightness` is a
     no-op per `LilyGoWatchUltra.h`'s `#ifdef ARDUINO` gating, same as factory's) |
| 6 | Reboot after closing tray | Confirm whether brightness persisted — determines if Step 1's
     dirty-flag question needs solving |

Dropped from the factory-plan matrix: horizontal icon-scroll (no icon grid here), low-power
save/restore (no such flag exists to test yet — see Step 4), Settings shortcut (no Settings screen
yet), msgbox interaction (no msgbox system yet). Re-add each as the corresponding subsystem gets
built in `custom_interface`.

## Suggested build order

0. Fix `usable_area` name bug, rewrite `main.ino` with the `ARDUINO` guard, add `main.cpp`. Verify
   the emulator boots and shows the existing button before touching tray code at all.
1. HAL shim: clock, battery, brightness (+ native stubs).
2. Tray object tree on `layer_top()`, hidden-when-closed, wired to a debug trigger (not gesture
   yet). Verify test 4 before adding any content — the layer_top hit-test trap is cheap to
   reintroduce and expensive to notice later.
3. Animation, state enum, the three close paths.
4. Gesture callback on `lv_screen_active()`. Verify tests 1–3.
5. Wire real content through the safe-area bands (Step 2); re-sync brightness on open.
6. Decide and implement Step 4's auto-close scope.
7. Full matrix on `emulator_watch_ultra`, then real T-Watch-Ultra hardware.

## Open risks

- **`custom_interface` doesn't build at all today.** Step 0 is blocking, not preparatory polish —
  budget real time for it, it's not a formality the way it was when this plan targeted `factory`.
- **No hardware build has been attempted** for any of this; only static analysis of the source
  tree and LilyGoLib headers went into this plan.
- **T-Watch-S3 needs `usable_area.cpp` board-gated first** — don't extend the tray there until
  `BEZEL_RADIUS` handling is fixed for a flat panel; treat that as separate, prerequisite work.
- **Whether `instance.setBrightness()` persists across reboot is unconfirmed** — check before
  deciding whether Step 1 needs its own persistence/dirty-flag story.
- **Volume and Wi-Fi are deferred, not designed.** Both need real subsystem bring-up
  (`instance.codec` init, `WiFi.begin()`) this app doesn't have; don't fake tiles for either.
- **No sleep/idle model exists to anchor auto-close to** — Step 4 is a genuine open decision, not
  a known quantity the way factory's `SCREEN_TIMEOUT` was.
