# How to place widgets through the safe-area engine

`usable_area.h` gives you containers that are already guaranteed to sit inside
the T-Watch-Ultra's curved bezel. This is the practical guide to using it for
your own widgets — buttons, labels, lists, whatever. For the math behind it
and how `BEZEL_RADIUS` was calibrated, see [`README.md`](README.md).

## The pattern

0. `#include <usable_area.h>` — it's a PlatformIO library under `lib/`, so the
   angle-bracket form works from any app at any nesting depth.
1. Call `usable_area_init()` once in `setup()`, after `beginLvglHelper()` and
   before creating any widgets.
2. For each widget, ask the engine for a container instead of parenting to
   `lv_screen_active()` directly:

   ```c
   lv_obj_t *area = usable_area_place(parent, y, height);
   ```

   `y` is the container's top edge in screen coordinates; `height` is how
   tall it is. The engine works out how much width is actually visible over
   that y-range and returns a container already sized and positioned to fit
   it.
3. **Check for `NULL`.** A band that falls entirely inside a corner (e.g.
   `y = 0, height = 10`) has no visible width at all, and `usable_area_place()`
   returns `NULL` rather than a zero-width container.
4. Create your widget as a child of `area`, not of `lv_screen_active()`.

```c
void setup()
{
    // ...
    beginLvglHelper(instance);
    usable_area_init();

    lv_obj_t *area = usable_area_place(lv_screen_active(), 6, 40);
    if (area) {
        lv_obj_t *btn = lv_button_create(area);
        lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "tap me");
        lv_obj_center(label);
    }
}
```

That's the whole contract. Everything below is what changes depending on
what you're placing.

## Sizing the widget inside its container

`usable_area_place()` only guarantees the *container* is safe — what you put in
it is up to you:

- **Fill it:** `lv_obj_set_size(widget, LV_PCT(100), LV_PCT(100))`, as in the
  button example above. Good for buttons and anything meant to span the row.
- **Center something smaller:** create the widget with an explicit size, then
  `lv_obj_center(widget)`. Good for a label or icon that shouldn't stretch.
- **Let it size itself:** labels with `LV_LABEL_LONG_WRAP` will wrap to the
  container's width automatically if you set `lv_obj_set_width(label,
  LV_PCT(100))` and leave the height unset.

A container is a plain `lv_obj_t` — normal LVGL clipping applies, so a child
that overflows it (e.g. a label with `LV_LABEL_LONG_CLIP`) is cut off at the
container's edge, not the bezel's. That's expected: the engine only promises
the container itself doesn't cross into the bezel.

## Stacking multiple widgets

`usable_area_place()` doesn't do layout — you still track the vertical cursor
yourself, the same way the demo sketch's band loop does:

```c
int32_t y = 10;
lv_obj_t *area;

area = usable_area_place(lv_screen_active(), y, 40);
if (area) {
    lv_obj_t *btn = lv_button_create(area);
    lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));
}
y += 40 + 8;  // height + gap

area = usable_area_place(lv_screen_active(), y, 60);
if (area) {
    lv_obj_t *label = lv_label_create(area);
    lv_label_set_text(label, "second row");
    lv_obj_center(label);
}
y += 60 + 8;
```

Each call is independent — nothing carries over between them except the `y`
you pass in yourself.

## A fixed-position widget instead of a row

If a widget doesn't need to hug a particular vertical position — a dialog,
a centered icon, anything that just needs to be "somewhere safe" — reach for
`usable_area_rect()` instead of computing a `y`/`height`:

```c
lv_obj_t *safe = usable_area_rect(lv_screen_active());
lv_obj_t *msg = lv_label_create(safe);
lv_label_set_text(msg, "always inside the curve");
lv_obj_center(msg);
```

It trades away the extra width `usable_area_place()` reclaims near the vertical
middle, but you don't have to think about `y` at all.

## Full-bleed: pad the content, not the container

Every pattern above wraps your widget in a container that's shrunk to fit the
curve, which is correct but costs you real screen area: a `usable_area_rect()`
button gives up the same 36 px margin on every side whether or not anything
is actually there to be clipped. For a widget that wants to cover the *whole*
panel — a tileview, a full-screen list, anything where the background,
scrollbar, or swipe/drag area should reach the true edge of the glass — that
margin is wasted, because the corners get hidden by the screen's own rounding
regardless of what draws into them.

`usable_area_init()` already sets `radius = BEZEL_RADIUS` and `clip_corner` on
the screen itself (see `usable_area.cpp`). So instead of asking the engine for
a smaller container, parent the widget straight to `lv_screen_active()` at
full size and let that clipping do the work visually. Then pad only the
*content* inside it — labels, buttons, list rows, anything readable or
tappable — by `SAFE_INSET`, so nothing lands under the bezel even though the
background behind it does:

```c
lv_obj_t *panel = lv_obj_create(lv_screen_active());
lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
lv_obj_set_style_pad_all(panel, 0, 0);   /*full-bleed background*/

lv_obj_t *label = lv_label_create(panel);
lv_label_set_text(label, "always inside the curve");
lv_obj_set_style_pad_all(label, SAFE_INSET, 0);   /*content stays safe*/
lv_obj_center(label);
```

The content box you end up with is the same 338×430 `usable_area_rect()` gives
you — this doesn't reclaim any *usable* space — but the background, and
anything scrollable, now runs edge to edge instead of stopping at a container
boundary that was never actually visible as a boundary to begin with.

**This only works for passive area.** Clipping hides drawing, not hit-testing
(see the Gotchas section below). A tileview or a plain background is fine
full-bleed because dragging/scrolling doesn't depend on landing inside a
precise rectangle — but an individual button or other tappable widget must
still live inside the `SAFE_INSET`-padded content, not out on the unpadded
full-bleed parent, or its hit box will reach into the bezel's dead zone.

## Checking whether something fits before creating it

If you want to know the visible width at a given row without creating a
container — e.g. to decide whether a widget should even attempt to render, or
to lay out something the engine's simple rectangle can't express — call the
inset functions directly:

```c
int32_t inset = usable_area_inset_for_band(y, y + height - 1);
int32_t width = usable_area_screen_width() - 2 * inset;
```

This is what `usable_area_place()` does internally; the demo sketch
also calls `usable_area_inset_for_band()` directly to decide whether a band
counts as "reclaimed" space (narrower than the fixed safe rect would allow).
## Gotchas

- **Don't parent *tappable* widgets to `lv_screen_active()` unpadded.** That's
  exactly the bug this engine exists to prevent — nothing stops the widget
  from landing under the bezel. Full-bleed background/gesture surfaces are
  the one deliberate exception (see above), and even those must keep their
  actual content inset by `SAFE_INSET`.
- **Call `usable_area_init()` before creating anything.** It sets
  `screen_w`/`screen_h` that every other function depends on; calling
  `usable_area_place()` first uses stale (zero) values.
- **A `NULL` from `usable_area_place()` is normal**, not an error — it just
  means that row is entirely hidden by the bezel. Always check before using
  the result.
- **Interactive widgets are still hit-tested as rectangles.** Clipping hides
  the *drawing* of anything that pokes past a container's edge, but LVGL's
  default hit test doesn't know about the curve. This mostly doesn't come up
  when you use `usable_area_place()`/`usable_area_rect()` properly, since the
  container itself won't cross into the bezel — but if you ever do let a
  widget extend past its safe-area container, see the "Clipping as a safety
  net" section in `README.md` for `LV_OBJ_FLAG_ADV_HITTEST`.
