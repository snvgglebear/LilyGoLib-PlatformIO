#pragma once

#include <lvgl.h>
#include <stdint.h>

/**
 * Safe-area engine for curved-bezel panels (the T-Watch-Ultra).
 *
 * The Ultra panel reports 410x502, but the cover glass is curved: anything
 * outside a ~120 px corner radius is hidden by the bezel. Call
 * usable_area_init() once (after beginLvglHelper()), then parent widgets to
 * a container from usable_area_place() or usable_area_rect() rather than
 * straight to lv_screen_active().
 *
 * Which one depends on the shape of what you are placing:
 *
 *   usable_area_place() - a horizontal band (a status bar, a row of buttons).
 *       Widest for short bands near the vertical middle, and it narrows
 *       sharply as the band reaches into a corner arc.
 *   usable_area_rect()  - anything full-height or full-screen (a tileview,
 *       a list, a whole app page). A full-height band is pinched to 170 px
 *       by its top row, so usable_area_place() is the wrong tool there;
 *       usable_area_rect() gives the 338x430 inscribed rect instead.
 *
 * On flat-panel boards (T-Watch-S3, T-LoRa-Pager) BEZEL_RADIUS defaults to 0
 * and every function degrades to a no-op: the insets are 0, place() returns a
 * full-width band and rect() the full screen. Callers therefore do not need
 * to guard use of this header behind a board #ifdef.
 *
 * See README.md alongside this header for the underlying math and how to
 * calibrate BEZEL_RADIUS against real hardware, and HOWTO.md for placing
 * your own widgets through the engine.
 */

/*The corner radius the curved glass hides everything outside of. Override
  per-env with -D BEZEL_RADIUS=<px> when calibrating against real hardware.*/
#ifndef BEZEL_RADIUS
#  if defined(ARDUINO_T_WATCH_S3_ULTRA)
#    define BEZEL_RADIUS    120
#  else
#    define BEZEL_RADIUS    0       /*flat panel: no bezel to lay out around*/
#  endif
#endif

/*Largest axis-aligned rect inside a rounded rect: its corners land on the arc
  when the inset is r * (1 - 1/sqrt(2)). 36 px at r=120. Rounded up.*/
#if BEZEL_RADIUS > 0
#  define SAFE_INSET    ((int32_t)(BEZEL_RADIUS * 0.29289322f) + 1)
#else
#  define SAFE_INSET    ((int32_t)0)
#endif

/*Call once after beginLvglHelper(): reads the panel resolution and styles
  the screen root (background, clipping to the rounded shape).*/
void usable_area_init(void);

/*Applies the same base styling usable_area_init() gives the boot screen
  (black background, no padding/border/scroll, clipped to the rounded
  shape) to any other screen object -- e.g. one created with
  lv_obj_create(NULL) for a second lv_screen_load()-able screen -- so it
  matches instead of showing the LVGL theme's default look.*/
void usable_area_style_screen(lv_obj_t *screen);

int32_t usable_area_screen_width(void);
int32_t usable_area_screen_height(void);

/*Horizontal inset required at vertical offset y (0 = top of the screen) for
  a point to stay inside the rounded viewport. Zero between the two arcs.*/
int32_t usable_area_inset_at(int32_t y);

/*Inset for a band spanning y_top..y_bot - the binding constraint is
  whichever of its two edges sits deeper into a corner. Use this, not
  usable_area_inset_at(), for anything with height.*/
int32_t usable_area_inset_for_band(int32_t y_top, int32_t y_bot);

/*The largest axis-aligned rect that fits inside the rounded viewport,
  centered on parent. Provably safe everywhere, at the cost of the
  reclaimable space near the vertical middle.*/
lv_obj_t *usable_area_rect(lv_obj_t *parent);

/*A container spanning y..y+height, widened to exactly the width visible at
  that band. Parent any widget (button, label, list, ...) to the returned
  object and it is guaranteed not to render under the bezel.

  Note the arguments are a vertical extent, NOT a size: the width is derived
  from the curve, never passed in. For full-height content use
  usable_area_rect() - see the header comment above.

  y      - top of the band, in px from the top of the screen
  height - height of the band in px
  Returns NULL if the band lies entirely in the bezel.*/
lv_obj_t *usable_area_place(lv_obj_t *parent, int32_t y, int32_t height);
