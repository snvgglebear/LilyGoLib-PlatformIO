# Plan: make the Gadgetbridge UI use the full usable area

`gb_ui_begin()` builds its entire persistent screen (status bar + tabview)
inside a single `usable_area_rect()`. That call is the right tool for a
fixed-size overlay (it's exactly what the thread view and the msgbox popups
need), but it's the wrong shape for the *whole* screen: it trades away real
usable space in a way `usable_area.cpp`'s other primitives
(`usable_area_place()`, `usable_area_inset_for_band()`) exist specifically to
avoid.

## 1. Why `usable_area_rect()` under-uses the panel

`usable_area_rect()` returns the largest axis-aligned rectangle that fits
inside the curved bezel *no matter where on it you look* - one inset,
applied on all four sides, sized so the rectangle's own corners just touch
the arc:

```
SAFE_INSET = BEZEL_RADIUS * (1 - 1/sqrt(2)) + 1   // ~36px at BEZEL_RADIUS=120
```

That formula only works by shrinking the rectangle on *every* side,
including top and bottom. Concretely, on the Ultra's 410x502 panel
(`usable_area.h`'s own numbers) with `BEZEL_RADIUS = 120`:

| Region | What `usable_area_rect()` does |
|---|---|
| y = 0..36 and y = 466..502 (72px total) | **Excluded entirely** - the rect starts at y=36 and ends at y=466, so this vertical space is never used at all. |
| y = 36..466 (the rest) | Inset 36px on both sides -> width capped at 338px, uniformly, even in the middle of the screen where the panel is already at full width. |

`usable_area_inset_at(y)` (already written, already used by `usable_area_place()`
for the popups' one-off placements) says the *actual* required inset only
gets that large near the very top/bottom corners - it's 0 for the whole
flat section between the two arcs (`y` outside `[0, BEZEL_RADIUS)` and
`[screen_h - BEZEL_RADIUS, screen_h)`, i.e. roughly y = 120..382 here). So
for most of the screen's height, content built inside `usable_area_rect()` is
~72px narrower than it needs to be, and the screen is ~72px shorter than it
needs to be - both at once, because one flat number is doing the job of
what should be a curve-shaped one.

The individual popups (`makeSafeMsgbox()`) already size against
`usable_area_screen_width()/height()` rather than nesting inside
`usable_area_rect()`, so they're a smaller version of the same
give-something-up-everywhere shape, just for a floating box rather than the
whole screen. This plan is only about the persistent screen scaffold built
in `gb_ui_begin()` - the popups are already using the API in the way it's
meant to be used and aren't in scope here (worth a follow-up pass later if
it turns out they also read as cramped).

## 2. What "fully using the usable area" looks like instead

Split the screen into three vertical bands with known, fixed heights instead
of one uniformly-inset region, and size each band's width to *its own*
worst-case inset via `usable_area_inset_for_band()` (what `usable_area_place()`
already wraps) instead of the whole-screen worst case:

```
y = 0 .. header_h            status bar   (deep in the curve - narrow, but
                                            it only holds two small labels)
y = header_h .. footer_y      content      (mostly the flat section - close
                                            to full width for nearly all of it)
y = footer_y .. screen_h      tab bar      (deep in the curve - narrow, but
                                            it only holds icon+label tabs)
```

The header and footer bands don't get *wider* than today - they're still
deep in the curved region and still need a real inset. What changes is that
they stop forcing that same inset onto the content band, which is the one
that actually holds lists, bubbles, and buttons and benefits from the extra
~72px of width.

### The content band's own top edge still matters

Naively starting the content band right where the header ends (e.g. y=30,
if the header is only as tall as today's status bar) doesn't help much -
`usable_area_inset_for_band()` over y=30..(screen_h - footer_h) is *dominated
by* the inset at y=30, which is still deep in the curve (roughly 40px at
y=30, worse than today's flat 36px). The fix isn't a smaller header, it's
sizing the header so the content band's top edge clears most of the curve
before it starts - i.e. deliberately let the header extend further down
into "already pretty open" territory (inset drops off fast: it's under 5px
by about y=90 at this radius) rather than stopping the header at the
smallest height its own content needs.

Concretely: pick `header_h` (and the matching `footer` band height, working
up from the bottom) close enough to `BEZEL_RADIUS` that
`usable_area_inset_for_band(header_h, screen_h - footer_h - header_h)`
comes out small - not necessarily zero, but a lot closer to it than 36px.
The header/footer then have some empty-looking vertical space in them next
to the status text / tab icons; that's the same trade as today's, just
moved to the two bands that were always going to be narrow anyway instead of
being spent on the whole screen.

## 3. Restructuring `gb_ui_begin()`

Today:

```cpp
lv_obj_t *screen = usable_area_rect(lv_screen_active());
lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
buildStatusBar(screen);
lv_obj_t *tabview = lv_tabview_create(screen);
...
```

Target shape:

```cpp
// no insets on the root itself - usable_area_init() already painted and
// clipped it; individual bands below pick their own widths.
lv_obj_t *screen = lv_screen_active();

lv_obj_t *header = usable_area_place(screen, 0, HEADER_H);
buildStatusBar(header);

lv_obj_t *content = usable_area_place(screen, HEADER_H, CONTENT_H);
lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
lv_obj_t *tabview = lv_tabview_create(content);
... (unchanged from here down)

lv_obj_t *footer_spacer ... // the tab bar is lv_tabview's own bottom strip,
                             // see open question below
```

`buildStatusBar()`'s internals don't need to change - it already lays out
with `LV_PCT(100)` and flex, so it just inherits whatever width `header`
(via `usable_area_place()`) hands it. Same for the tab pages under `content`.

`HEADER_H` and `CONTENT_H` become the knobs: add them next to those constants, sized so
`usable_area_inset_for_band()` over the content band comes out small (see
§2). This needs a hardware/emulator round of eyeballing to land on numbers
that look right rather than deriving them purely on paper - the constants
in `gb_ui_metrics.h` were clearly tuned that way too (the per-board large/small
pairs differ by eyeballed amounts, not by a formula in the file).

## 4. ~~Open question: the tab bar~~ -- resolved, no longer applies

**Superseded.** This section asked how to place `lv_tabview`'s own bottom tab
bar, which the app cannot position itself, given it sits in the bezel's bottom
curve where the panel is narrowest.
[`gadgetbridge-button-grid-nav.md`](gadgetbridge-button-grid-nav.md) has since
removed the tab bar entirely -- navigation is a launcher grid on page 0 plus a
home button in the status bar -- so the Gadgetbridge screen is now status bar +
content, exactly the two-band shape section 2 wants, with no footer band to
resolve. `GB_TAB_BAR_HEIGHT_LARGE`/`_SMALL` no longer exist in
`gb_ui_metrics.h`; references to them in section 3 are historical.

## 5. Testing

`emulator_watch_ultra` (`pio run -e emulator_watch_ultra -t exec`) renders
this exact 410x502 panel shape on the host via SDL2 - no hardware round
trip needed to see whether a given `HEADER_H`/`CONTENT_H` choice actually
looks right, unlike the wrist-tilt work. Iterate the constants there; only
confirm the final numbers on the real Ultra before calling it done, since
the emulator's SDL2 rendering of `clip_corner` / the rounded mask may not
be pixel-identical to the real cover glass's optical curve.

## 6. Risks / non-goals

- This only touches `gb_ui_begin()`'s top-level scaffold. `makeSafeMsgbox()`
  and the thread view (`openThread()`, already using `usable_area_rect()`
  deliberately for a floating overlay) are out of scope - flag as a
  possible follow-up, don't fold into this change.
- `s_small_screen` (T-Watch-S3, 240x240, presumably no curved bezel /
  different `BEZEL_RADIUS` handling) needs its own header/content numbers,
  not the Ultra's - check whether `BEZEL_RADIUS`/`SAFE_INSET` even apply
  there before assuming this plan's math carries over unchanged.
- Any content that currently assumes it's centered inside a shorter,
  narrower `usable_area_rect()` (padding, alignment) will need re-checking
  against the new wider/taller bounds - `buildWatchTab()`'s centered flex
  layout in particular, since its column will now be visibly wider.
