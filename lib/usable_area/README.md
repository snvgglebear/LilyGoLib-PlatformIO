# `usable_area` — laying out around the curved bezel

The T-Watch-Ultra panel reports **410 × 502**, but the cover glass is curved:
everything outside a corner radius of roughly **120 px** is hidden behind the
bezel. Widgets placed near a corner are drawn correctly and simply never seen.

`usable_area.h`/`usable_area.cpp` is a small reusable engine for laying out inside
that boundary — a fixed safe rectangle, per-band insets that reclaim the space
the fixed rectangle wastes, and a full-bleed pattern for widgets that should
cover the whole panel. See [`HOWTO.md`](HOWTO.md) for how to place your own
widgets through the engine.

## Using it

This is a PlatformIO local library under `lib/`, so every env picks it up
automatically for whatever `src_dir` is selected. From any app, at any nesting
depth:

```c
#include <usable_area.h>
```

The library is only linked into apps that actually include it, so apps that
don't use it pay nothing.

`BEZEL_RADIUS` defaults per board: 120 on `ARDUINO_T_WATCH_S3_ULTRA`, and **0**
everywhere else (T-Watch-S3, T-LoRa-Pager — flat panels with no bezel to lay
out around). At radius 0 every function degrades to a no-op: the insets are 0,
`usable_area_place()` returns a full-width band and `usable_area_rect()` the
full screen. Callers therefore don't need to guard their use of this header
behind a board `#ifdef`.

## The demo sketch

[`src/testing_safe_area/safe_area.ino`](../../src/testing_safe_area/safe_area.ino)
is a harness that exercises the first two approaches below, and is what you
flash to calibrate `BEZEL_RADIUS` against real glass:

```bash
# in platformio.ini, swap the src_dir lines:
#   ;src_dir = src/testing
#   src_dir = src/testing_safe_area
pio run -e twatch_ultra -t upload
```

Or without touching the file:

```bash
PLATFORMIO_SRC_DIR=src/testing_safe_area pio run -e twatch_ultra -t upload
```

Only `twatch_ultra` is meaningful — the sketch is wrapped in
`#if defined(ARDUINO_T_WATCH_S3_ULTRA)` and compiles to nothing elsewhere.

## What you should see

A stack of rounded bands, each widened to exactly the width visible at its own
vertical position, so their ends trace the corner arcs.

- **Teal bands** are narrower than the fixed safe rectangle allows. Their extra
  width is the space a fixed rectangle throws away.
- **Grey bands** fit inside the fixed rectangle either way.
- The **amber outline** is the fixed safe rectangle (338 × 430), drawn over the
  bands for comparison, with a label reporting the numbers.

## Calibrating `BEZEL_RADIUS`

The whole engine is driven by one constant, defaulted per board in
`usable_area.h`:

```c
#define BEZEL_RADIUS 120
```

Override it per-env without editing the header by adding to that env's
`build_flags`:

```ini
-D BEZEL_RADIUS=118
```

Flash the demo sketch and look at the top and bottom bands:

| What you see | Meaning | Action |
|---|---|---|
| Bands end flush with the visible edge | Radius matches the glass | Done |
| Top/bottom bands look cut off | Radius too small — layout trusts space that isn't visible | Increase |
| Bands stop short of the edge | Radius too large — you're giving up visible space | Decrease |

Adjust, reflash, repeat. Everything else — the safe rectangle, the per-band
insets, the clipping — derives from this one number.

## The three approaches

### 1. Fixed safe rectangle

The largest axis-aligned rectangle inside a rounded rectangle has its corners
landing exactly *on* the arc when the inset is `r × (1 − 1/√2)` ≈ `0.293 r`.
At r = 120 that is **36 px**, giving a **338 × 430** safe area:

```c
#define SAFE_INSET ((int32_t)(BEZEL_RADIUS * 0.29289322f) + 1)
```

`usable_area_rect(parent)` returns a container covering it. Parent your widgets
to that and the corners stop being a concern. This is the one to reach for
first — it is provably safe, not a guess, and costs one container.

### 2. Per-band insets

A uniform 36 px inset wastes real estate in the vertical middle, where the full
410 px genuinely *is* visible. `usable_area_inset_at()` returns the inset
actually required at a given vertical offset:

```c
int32_t usable_area_inset_at(int32_t y);
```

| y | inset |
|---|---|
| 0 | 120 |
| 20 | 54 |
| 36 | 35 |
| 60 | 17 |
| 90 | 4 |
| 120 … 382 | 0 |

Between y = 120 and y = 382 it returns 0, so rows there use the full width —
72 px more than the fixed rectangle offers. Useful for lists where each row
sizes itself, or for anything meant to follow the curve.

**Use `usable_area_inset_for_band()`, not `usable_area_inset_at()`, for anything
with height.** A widget's top and bottom edges sit at different depths in the
arc, and the binding constraint is whichever is worse:

```c
int32_t usable_area_inset_for_band(int32_t y_top, int32_t y_bot);
```

Passing only one edge lets the other corner poke into the bezel.
`usable_area_place(parent, y, height)` wraps this — it builds the band's
container directly instead of just returning the inset. See
[`HOWTO.md`](HOWTO.md) for placing real widgets with it.

### 3. Full-bleed with padded content

Approaches 1 and 2 both size a *container* to fit the curve, which costs a
fixed margin whether or not anything is actually there to be clipped — a
full-height widget wrapped in `usable_area_rect()` gives up 36 px of background
on every side even though the corners get hidden by the screen's own rounding
regardless of what's drawn under them (see "Clipping as a safety net" below).

For a widget that should cover the whole panel — a tileview, a full-screen
list, anything where the background, scrollbar, or swipe/drag area should
reach the true edge of the glass — parent it directly to `lv_screen_active()`
at full size instead, and let that existing clipping do the visual work. Then
pad only the *content* (labels, buttons, list rows — anything readable or
tappable) by `SAFE_INSET`, so nothing lands under the bezel even though the
background behind it does. The usable content box ends up identical to
approach 1's 338 × 430; what changes is that the background and any
scroll/drag surface now run edge to edge instead of stopping at a container
boundary that was never visible as a boundary in the first place.

This only replaces the *container*, not the "don't cross the bezel" rule for
anything tappable — see the hit-testing note below, which is exactly why the
padding has to move to the content instead of just disappearing. See
[`HOWTO.md`](HOWTO.md) for a worked example.

## Clipping as a safety net

`usable_area_init()` applies this to the screen root, once, before any widgets
are created:

```c
lv_obj_set_style_radius(root, BEZEL_RADIUS, 0);
lv_obj_set_style_clip_corner(root, true, 0);
```

This masks children to the rounded shape during rendering. It is a backstop for
layout mistakes, not a layout solution — it makes overflow *invisible* rather
than preventing it.

**It does not affect hit testing.** LVGL's default hit test is rectangular
(`lv_area_is_point_on(&a, point, 0)` in `lv_obj_pos.c`), so a button whose
corner is hidden under the bezel is still tappable in that invisible region.
If that matters, set `LV_OBJ_FLAG_ADV_HITTEST` on the object and handle
`LV_EVENT_HIT_TEST`.

## Why not a canvas

A canvas is a raster pixel buffer for immediate-mode drawing (`lv_draw_rect`,
`lv_draw_label`, …). It has no notion of widgets — you cannot place an
`lv_button` or `lv_list` on one — and a full-screen ARGB8888 canvas costs
410 × 502 × 4 = **823 kB**, which must come from PSRAM since the ESP32-S3 has
only 320 kB of internal DRAM. Building bezel handling into a custom canvas
means reimplementing LVGL's layout and event system to get widgets back.

The rounded viewport is a layout and clipping constraint, not a drawing one.

## One sketch per `src_dir`

The demo lives in its own `src/testing_safe_area/` directory rather than beside
`src/testing/testing.ino` because PlatformIO merges **every** `.ino` under
`src_dir` — recursively — into a single translation unit
(`InoToCPPConverter.merge()`). Two sketches in one tree collide on
`setup()`/`loop()`. Rename the folder freely; just keep one sketch per
`src_dir`. The engine itself is outside `src_dir` entirely now that it's a
library under `lib/`, so it never counts against that limit.

## Reference

- [`HOWTO.md`](HOWTO.md) — placing your own widgets through the engine
- [LVGL docs](https://lvgl.io/docs/open) — `lv_obj_set_style_clip_corner`,
  `LV_OBJ_FLAG_ADV_HITTEST`
- [`src/testing/testing.ino`](../../src/testing/testing.ino) — the canvas
  experiment the 120 px radius was originally measured with, and
  `lv_example_tileview_full_bleed()` for a worked example of approach 3
  against a real widget (a tileview)
