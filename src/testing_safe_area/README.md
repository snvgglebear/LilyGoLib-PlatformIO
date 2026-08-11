# `src/testing_safe_area` — laying out around the curved bezel

The T-Watch-Ultra panel reports **410 × 502**, but the cover glass is curved:
everything outside a corner radius of roughly **120 px** is hidden behind the
bezel. Widgets placed near a corner are drawn correctly and simply never seen.

This sketch makes that boundary visible and gives you two ways to lay out
inside it — a fixed safe rectangle, and per-band insets that reclaim the space
the fixed rectangle wastes.

## Build

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

The whole sketch is driven by one constant:

```c
#define BEZEL_RADIUS 120
```

Flash it and look at the top and bottom bands:

| What you see | Meaning | Action |
|---|---|---|
| Bands end flush with the visible edge | Radius matches the glass | Done |
| Top/bottom bands look cut off | Radius too small — layout trusts space that isn't visible | Increase |
| Bands stop short of the edge | Radius too large — you're giving up visible space | Decrease |

Adjust, reflash, repeat. Everything else — the safe rectangle, the per-band
insets, the clipping — derives from this one number.

## The two approaches

### 1. Fixed safe rectangle

The largest axis-aligned rectangle inside a rounded rectangle has its corners
landing exactly *on* the arc when the inset is `r × (1 − 1/√2)` ≈ `0.293 r`.
At r = 120 that is **36 px**, giving a **338 × 430** safe area:

```c
#define SAFE_INSET ((int32_t)(BEZEL_RADIUS * 0.29289322f) + 1)
```

`safe_area_create()` returns a container covering it. Parent your widgets to
that and the corners stop being a concern. This is the one to reach for first —
it is provably safe, not a guess, and costs one container.

### 2. Per-band insets

A uniform 36 px inset wastes real estate in the vertical middle, where the full
410 px genuinely *is* visible. `bezel_inset_at()` returns the inset actually
required at a given vertical offset:

```c
static int32_t bezel_inset_at(int32_t y);
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

**Use `bezel_inset_for_band()`, not `bezel_inset_at()`, for anything with
height.** A widget's top and bottom edges sit at different depths in the arc,
and the binding constraint is whichever is worse:

```c
static int32_t bezel_inset_for_band(int32_t y_top, int32_t y_bot);
```

Passing only one edge lets the other corner poke into the bezel.

## Clipping as a safety net

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

This lives in its own directory rather than beside `src/testing/testing.ino`
because PlatformIO merges **every** `.ino` under `src_dir` — recursively — into
a single translation unit (`InoToCPPConverter.merge()`). Two sketches in one
tree collide on `setup()`/`loop()`. Rename the folder freely; just keep one
sketch per `src_dir`.

## Reference

- [LVGL docs](https://lvgl.io/docs/open) — `lv_obj_set_style_clip_corner`,
  `LV_OBJ_FLAG_ADV_HITTEST`
- [`src/testing/testing.ino`](../testing/testing.ino) — the canvas experiment
  the 120 px radius was originally measured with
