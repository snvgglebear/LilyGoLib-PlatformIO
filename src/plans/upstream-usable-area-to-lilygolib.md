# Contributing `usable_area` upstream to LilyGoLib

Plan for taking `lib/usable_area` — this repo's curved-bezel safe-area layout
engine — and offering it to [Xinyuan-LilyGO/LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib)
as a first-class part of the board support library.

**Status:** not started. Written 2026-08-17, against LilyGoLib V0.2.0 as
fetched into `.pio/libdeps/twatch_ultra/LilyGoLib`.

## Why it belongs upstream

The problem is a property of the hardware LilyGoLib exists to abstract, not of
any one app: the T-Watch-Ultra panel reports 410×502, but the cover glass hides
everything outside a ~120 px corner radius. Every LVGL app anyone writes for
that board hits this, and today each one rediscovers it and hardcodes its own
constant. LilyGoLib already owns the neighbouring abstractions — `LilyGo_Display`
exposes `width()`/`height()`, `LV_Helper` owns LVGL bring-up — so the bezel
radius is a missing field in a struct upstream already maintains.

It's also cheap for them to take: pure LVGL plus `sqrtf`, no new dependency, no
new hardware access, ~90 lines, and it is inert (`radius = 0`) on the two flat
boards.

## The one blocking design change

Our version resolves `BEZEL_RADIUS` at **compile time** from the board macro:

```c
#ifndef BEZEL_RADIUS
#  if defined(ARDUINO_T_WATCH_S3_ULTRA)
#    define BEZEL_RADIUS 120
#  else
#    define BEZEL_RADIUS 0
#  endif
#endif
```

That's right for an app repo that builds one board at a time. It is wrong for a
library: LilyGoLib's whole design is a runtime `LilyGo_Display` polymorphic
base, and a `#ifdef` on board identity inside it would be the odd one out.
Upstream will want the radius to come from the display object.

**Change to make before proposing:** add a non-pure virtual to `LilyGo_Display`
in `src/LilyGoDispInterface.h`, alongside the existing `width()`/`height()`:

```cpp
/// Corner radius in px hidden by curved cover glass. 0 = flat panel.
virtual uint16_t bezelRadius() { return 0; }
```

Non-pure and defaulted to 0, so no existing board subclass breaks and no other
file has to change. Then `LilyGoWatchUltra` overrides it to return 120, and
`LilyGoWatchS3` / `LilyGo_LoRa_Pager` inherit the default and need no edit at
all. This is a strictly additive change to a public header — the easiest kind
to get accepted.

### Consequence: `SAFE_INSET` stops being a macro

Once the radius is runtime, `SAFE_INSET` can no longer be a constant
expression. It becomes a function:

```cpp
int32_t lv_safe_area_inset(void);   // was: #define SAFE_INSET
```

That is an API break for **this** repo — 10 call sites across
`src/custom_interface` and `src/new_interface` use `SAFE_INSET` in layout
expressions. Handle it with a one-line compat shim in our own tree rather than
touching all ten:

```c
#define SAFE_INSET (lv_safe_area_inset())
```

Keep the shim in a local `lib/usable_area/src/usable_area.h` that forwards to
the upstream header, so our apps keep compiling unchanged whichever way the
upstream proposal goes.

## Keeping the emulator working

`lib/usable_area` currently compiles for the `emulator_*` (native/SDL2) envs
because it depends on nothing but `<lvgl.h>` and `<math.h>`. Upstream's
`LV_Helper.h` includes `<Arduino.h>`, so folding the engine directly into it
would cost us the emulator.

Propose it as two layers instead:

1. `src/LV_SafeArea.h` / `.cpp` — the engine, taking a plain radius. Depends on
   LVGL only, so it stays native-compilable:
   ```cpp
   void lv_safe_area_init(uint16_t bezel_radius);
   ```
2. A one-line convenience overload in `LV_Helper` for the normal Arduino path,
   which is what the examples and most users would call:
   ```cpp
   void beginSafeArea(LilyGo_Display &display);   // -> lv_safe_area_init(display.bezelRadius())
   ```

This is worth stating explicitly in the proposal — it's the difference between
a change we can keep using and one we'd have to fork around.

## LVGL v8 compatibility

LilyGoLib ships both `LV_Helper.cpp` and `LV_Helper_v9.cpp`, and both
`lv_conf.h` and `lv_conf.h.v8` — so it still supports LVGL 8. Our engine is
LVGL 9-only:

| Ours (v9) | v8 equivalent |
|---|---|
| `lv_display_get_horizontal_resolution()` | `lv_disp_get_hor_res()` |
| `lv_screen_active()` | `lv_scr_act()` |
| `lv_obj_remove_flag()` | `lv_obj_clear_flag()` |

Three calls. Either guard them with `#if LVGL_VERSION_MAJOR >= 9` or ask the
maintainer whether new code still needs to carry v8. **Ask before writing the
shims** — if v8 is on the way out, the guards are dead weight that makes the
diff harder to review.

## File placement

Following the conventions already in the tree:

| What | Where |
|---|---|
| Engine | `src/LV_SafeArea.h`, `src/LV_SafeArea.cpp` |
| Virtual + Ultra override | `src/LilyGoDispInterface.h`, `src/LilyGoWatchUltra.h/.cpp` |
| Example | `examples/lvgl/safe_area/` (sketch + `ci.json`) |
| Docs | a section in `docs/lilygo-t-watch-ultra.md` |

- `library.json`'s `export.include` already globs `src/*` and `examples/*`, so
  new files there need no manifest edit.
- Every file needs upstream's header block: `@file` / `@author` / `@license MIT`
  / `@copyright` — match `src/LV_Helper.h` exactly.
- Upstream naming is `beginLvglHelper()`-style camelCase for free functions in
  `LV_Helper`, and `lv_`-prefixed snake_case where it mirrors LVGL's own API.
  Our `usable_area_*` names match neither; rename to `lv_safe_area_*` for the
  engine and camelCase for the `LV_Helper` convenience call.

### The example and CI

`src/testing_safe_area/safe_area.ino` becomes `examples/lvgl/safe_area/safe_area.ino`.
Examples are CI-built per board via `.github/workflows/lvgl_examples_ci.yml`
and `.github/scripts/check_ci_json.py`, which reads a per-example `ci.json`.
Ours only makes sense on the Ultra:

```json
{
  "targets": {
    "tlora_pager": false,
    "twatch_ultra": true,
    "twatchs3": false
  },
  "active_radio": "Radio_SX1262"
}
```

Drop the `#if defined(ARDUINO_T_WATCH_S3_ULTRA)` wrapper from the sketch when
adding this — `ci.json` is how upstream expresses board applicability, and the
wrapper would make the other boards build an empty binary rather than skip.

## Sequencing

Do the de-risking work locally first: everything up to step 3 is useful to this
repo whether or not upstream ever merges it.

- [ ] **1. Make our engine runtime-radius.** Replace the `BEZEL_RADIUS` macro
      with an `lv_safe_area_init(uint16_t radius)` parameter and
      `lv_safe_area_inset()`, keeping `#define SAFE_INSET (lv_safe_area_inset())`
      and `#define BEZEL_RADIUS` shims so our 20 call-site files don't move.
      Verify with the same matrix used for the library migration: 4 apps ×
      3 boards, plus `emulator_watch_ultra`.
- [ ] **2. Rename to upstream conventions** (`lv_safe_area_*`), with our local
      header keeping `usable_area_*` as inline forwarders. Now our tree and the
      proposed upstream tree differ only by file location.
- [ ] **3. Prototype against a local LilyGoLib checkout.** Clone the fork,
      point `lib_deps` at the local path, apply the `bezelRadius()` virtual and
      the new `src/LV_SafeArea.*`, and build this repo's apps against it. This
      is the real proof the design fits — do it before writing any issue text.
- [ ] **4. Open an issue on Xinyuan-LilyGO/LilyGoLib first, not a PR.** Lead
      with the problem (screenshot of a widget vanishing under the bezel),
      then the proposed `bezelRadius()` virtual, and ask two questions: does
      new code still need LVGL v8 support, and would they rather this be part
      of `LV_Helper` or its own module. A ~200-line unsolicited PR touching a
      public header is easy to ignore; an issue with a screenshot is not.
- [ ] **5. PR only after the maintainer responds**, split into two commits so
      the additive part can be merged even if the engine isn't:
      1. `LilyGoDispInterface`: add `bezelRadius()`, override in `LilyGoWatchUltra`.
      2. `LV_SafeArea` + example + `ci.json` + docs.
- [ ] **6. Whatever the outcome, keep `lib/usable_area`.** If upstream merges,
      it shrinks to a compatibility header forwarding to `<LV_SafeArea.h>`,
      and we drop it entirely once `lib_deps` moves past the release that
      includes it. If they decline, we've still ended up with a better,
      board-agnostic engine and lost nothing.

## Fallback if upstream declines

Publish `usable_area` as its own repo and consume it via `lib_deps` by URL.
`lib/usable_area/library.json` already carries the metadata that requires; the
only change is dropping the directory and adding one `lib_deps` line. Worth
doing only if someone outside this repo wants it — editing in place is more
convenient while we're still the only consumer.

## Reference

- [`lib/usable_area/README.md`](../../lib/usable_area/README.md) — the math and calibration
- [`lib/usable_area/HOWTO.md`](../../lib/usable_area/HOWTO.md) — placing widgets through the engine
- [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) — MIT, maintainer [lewisxhe](https://github.com/lewisxhe)
