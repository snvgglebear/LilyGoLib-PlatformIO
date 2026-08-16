#pragma once

#include <lvgl.h>
#include <stdint.h>

/**
 * Safe-area engine for the T-Watch-Ultra's curved bezel.
 *
 * The panel reports 410x502, but the cover glass is curved: anything
 * outside a ~120 px corner radius is hidden by the bezel. Call
 * safe_area_init() once (after beginLvglHelper()), then hand every widget's
 * parent creation to safe_area_place() instead of lv_screen_active() - it
 * returns a container already sized and positioned to stay inside the
 * visible curve for the y-range you ask for.
 *
 * See src/testing_safe_area/README.md for the underlying math and how to
 * calibrate BEZEL_RADIUS against real hardware.
 */

/*The corner radius the curved glass hides everything outside of.*/
#define BEZEL_RADIUS    120

/*Largest axis-aligned rect inside a rounded rect: its corners land on the arc
  when the inset is r * (1 - 1/sqrt(2)). 36 px at r=120. Rounded up.*/
#define SAFE_INSET      ((int32_t)(BEZEL_RADIUS * 0.29289322f) + 1)

/*Call once after beginLvglHelper(): reads the panel resolution and styles
  the screen root (background, clipping to the rounded shape).*/
void usable_area_init(void);

/*Applies the same base styling usable_area_init() gives the boot screen
  (black background, no padding/border/scroll, clipped to the rounded
  shape) to any other screen object -- e.g. one created with
  lv_obj_create(NULL) for a second lv_screen_load()-able screen -- so it
  matches instead of showing the LVGL theme's default look.*/
void usable_area_style_screen(lv_obj_t *screen);

int32_t safe_area_screen_width(void);
int32_t safe_area_screen_height(void);

/*Horizontal inset required at vertical offset y (0 = top of the screen) for
  a point to stay inside the rounded viewport. Zero between the two arcs.*/
int32_t safe_area_inset_at(int32_t y);

/*Inset for a band spanning y_top..y_bot - the binding constraint is
  whichever of its two edges sits deeper into a corner. Use this, not
  safe_area_inset_at(), for anything with height.*/
int32_t safe_area_inset_for_band(int32_t y_top, int32_t y_bot);

/*The largest axis-aligned rect that fits inside the rounded viewport,
  centered on parent. Provably safe everywhere, at the cost of the
  reclaimable space near the vertical middle.*/
lv_obj_t *safe_area_rect(lv_obj_t *parent);

/*A container spanning y..y+height, widened to exactly the width visible at
  that band. Parent any widget (button, label, list, ...) to the returned
  object and it is guaranteed not to render under the bezel. Returns NULL if
  the band lies entirely in the bezel.*/
lv_obj_t *safe_area_place(lv_obj_t *parent, int32_t y, int32_t height);
