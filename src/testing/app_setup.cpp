/**
 * @file      app_setup.cpp
 * @license   MIT
 * @brief     Shared UI + sleep handling for the testing sketch. See app_setup.h.
 *
 * Everything here is plain LVGL plus a thin platform shim, so it compiles for
 * both an ESP32 board and an emulator_* env. The hardware-only parts of the
 * original sketch (the BHI260 orientation sensor, the PMU event hook, serial
 * logging) stayed behind in testing.ino.
 */
#include "app_setup.h"

#include <usable_area.h>

#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#endif

/// How long the screen stays on with no activity before it sleeps.
#define SCREEN_SLEEP_TIMEOUT_MS  10000

/// Inset of the drawn rect inside the canvas, in px.
#define spacing 0

static bool power_button_clicked = false;
static bool screen_asleep = false;
static uint32_t last_activity_ms = 0;

// ---------------------------------------------------------------------------
// Platform shim
// ---------------------------------------------------------------------------
#ifdef ARDUINO

static uint32_t nowMs()       { return millis(); }
static bool     touched()     { return instance.getTouched(); }
static void     displaySleep(){ instance.sleepDisplay(); }
static void     displayWake() { instance.wakeupDisplay(); }

#else

/* LilyGoLib's library.json declares "frameworks": ["arduino"], so PlatformIO
   never adds its include path for the native platform the emulator envs build
   against -- <LilyGoLib.h> cannot be included here at all, guarded or not.
   There is no PMU and no backlight on the host either, so sleep/wake is
   tracked as state only and the SDL2 window's mouse stands in for touch.
   Same shape as src/custom_interface/screen_state/screen_state.cpp's native
   half. */

static uint32_t nowMs()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/// True if any pointer indev (the SDL2 window's mouse) is currently pressed.
static bool touched()
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            return true;
        }
    }
    return false;
}

static void displaySleep() { printf("[testing] display sleep (no backlight on the host)\n"); }
static void displayWake()  { printf("[testing] display wake\n"); }

#endif

/**
 * @title Rectangle with border and outline
 * @brief Draw a red rounded rectangle with a blue border and a green outline onto a canvas.
 *
 * A full-screen `LV_COLOR_FORMAT_ARGB8888` canvas is centered and filled with
 * a light grey background. An `lv_draw_rect_dsc_t` is populated with a red
 * fill, radius 120, a 3 px blue border, and a 2 px green outline at
 * `LV_OPA_50`, then painted via `lv_draw_rect` on a layer opened with
 * `lv_canvas_init_layer`.
 */
void lv_example_canvas_rectangle(void)
{
    /*Panel size read at runtime rather than #defined: the Ultra's panel is
      410x502 portrait, but emulator_watch_ultra's SDL window is 502x410, so
      one hardcoded pair would be wrong on one of the two targets.*/
    int32_t canvas_w = lv_display_get_horizontal_resolution(NULL);
    int32_t canvas_h = lv_display_get_vertical_resolution(NULL);

    static uint8_t *canvas_buf = NULL;
    if (!canvas_buf) {
#ifdef ARDUINO
        /*At full screen size this is 410*502*4 = 823 kB, far more than the
          ESP32-S3's 320 kB of internal DRAM, so it has to come from PSRAM.*/
        canvas_buf = (uint8_t *)ps_malloc(canvas_w * canvas_h * 4);
#else
        canvas_buf = (uint8_t *)malloc(canvas_w * canvas_h * 4);
#endif
        if (!canvas_buf) {
            /*LV_LOG rather than Serial: there is no Serial on the host.*/
            LV_LOG_WARN("Failed to allocate canvas buffer");
            return;
        }
    }

    /*Create a canvas and initialize its palette*/
    lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, canvas_buf, canvas_w, canvas_h, LV_COLOR_FORMAT_ARGB8888);

    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
    lv_obj_center(canvas);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_palette_main(LV_PALETTE_RED);
    dsc.border_color = lv_palette_main(LV_PALETTE_BLUE);
    dsc.border_width = 3;
    dsc.outline_color = lv_palette_main(LV_PALETTE_GREEN);
    dsc.outline_width = 2;
    dsc.outline_pad = 2;
    dsc.outline_opa = LV_OPA_50;
    dsc.radius = 120;

    /*lv_area_t is {x1, y1, x2, y2} - top-left and bottom-right corners,
      so x2 must be > x1 and y2 must be > y1 or nothing is drawn.*/
    lv_area_t coords = {spacing, spacing, canvas_w - spacing, canvas_h - spacing};

    lv_draw_rect(&layer, &dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

/*Same L-shaped tileview, but full-bleed: the tileview covers the whole panel
  so backgrounds, scrollbars and swipe gestures reach the edge of the glass,
  and only the tile *contents* are inset by SAFE_INSET to stay clear of the
  corner arcs. Whatever background does fall in a corner is hidden by the
  screen's clip_corner, set in usable_area_init().

  Costs nothing in usable content area versus usable_area_rect() - the content
  box is the same 338x430 - but avoids the 36 px black border around it.*/
void lv_example_tileview_full_bleed(void)
{
    lv_obj_t * tv = lv_tileview_create(lv_screen_active());

    /*Tiles are sized and positioned as a percentage of the tileview's content
      area, so the tileview itself must not carry the theme's default padding
      or the tiles stop matching the panel.*/
    lv_obj_set_style_pad_all(tv, 0, 0);

    /*Tile1: just a label*/
    lv_obj_t * tile1 = lv_tileview_add_tile(tv, 0, 0, (lv_dir_t)(LV_DIR_BOTTOM | LV_DIR_LEFT));
    lv_obj_set_style_pad_all(tile1, SAFE_INSET, 0);
    lv_obj_t * label = lv_label_create(tile1);
    lv_label_set_text(label, "Scroll down");
    lv_obj_center(label);

    /*Tile2: a button*/
    lv_obj_t * tile2 = lv_tileview_add_tile(tv, 0, 1, (lv_dir_t)(LV_DIR_TOP | LV_DIR_RIGHT));
    lv_obj_set_style_pad_all(tile2, SAFE_INSET, 0);

    lv_obj_t * btn = lv_button_create(tile2);

    label = lv_label_create(btn);
    lv_label_set_text(label, "Scroll up or right");

    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(btn);

    /*Tile3: a list. This tile keeps zero padding so the list's background runs
      edge to edge; the inset goes on the list instead, so its buttons stay off
      the arcs. LVGL clamps scrolling to the padded content area, so the first
      and last rows cannot ride up into a corner either.*/
    lv_obj_t * tile3 = lv_tileview_add_tile(tv, 1, 1, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_TOP));
    lv_obj_set_style_pad_all(tile3, 0, 0);
    lv_obj_t * list = lv_list_create(tile3);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(list, SAFE_INSET, 0);
    lv_obj_set_style_radius(list, 120, 0);

    lv_list_add_button(list, NULL, "One");
    lv_list_add_button(list, NULL, "Two");
    lv_list_add_button(list, NULL, "Three");
    lv_list_add_button(list, NULL, "Four");
    lv_list_add_button(list, NULL, "Five");
    lv_list_add_button(list, NULL, "Six");
    lv_list_add_button(list, NULL, "Seven");
    lv_list_add_button(list, NULL, "Eight");
    lv_list_add_button(list, NULL, "Nine");
    lv_list_add_button(list, NULL, "Ten");

    // Tile 4: a Label.
    lv_obj_t *tile4 = lv_tileview_add_tile(tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_BOTTOM));

    lv_obj_set_style_pad_all(tile4, SAFE_INSET, 0);
    lv_obj_t *label4 = lv_label_create(tile4);
    lv_label_set_text(label4, "Scroll Left or Down");
    lv_obj_set_style_pad_all(label4, SAFE_INSET, 0);
}

void CreateButtonGrid(void)
{
    lv_obj_t * screen = lv_screen_active();

    /* One row with four differently-flagged buttons */
    lv_obj_t * buttonmatrix = lv_buttonmatrix_create(screen);
    lv_obj_set_style_pad_all(buttonmatrix, 0, 0);
    lv_obj_set_style_pad_row(buttonmatrix, SAFE_INSET, 0);
    lv_obj_set_style_pad_column(buttonmatrix, SAFE_INSET, 0);
    lv_obj_set_align(buttonmatrix, LV_ALIGN_CENTER);
    lv_obj_set_size(buttonmatrix, lv_pct(90), 150);
    static const char * buttonmatrix_map_0[] = {"Normal", "Checked", "Disabled", "Hidden", NULL};
    lv_buttonmatrix_set_map(buttonmatrix, buttonmatrix_map_0);
    /*(lv_buttonmatrix_ctrl_t)0 rather than LV_BUTTONMATRIX_CTRL_NONE: that enumerator
      exists in the 9.5.0 LVGL the hardware envs resolve, but not in the 9.2.2 the
      emulator envs pin. 0 is its value in both.*/
    static const lv_buttonmatrix_ctrl_t buttonmatrix_ctrl_map_1[] = {(lv_buttonmatrix_ctrl_t)0, (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CHECKED), LV_BUTTONMATRIX_CTRL_DISABLED, LV_BUTTONMATRIX_CTRL_HIDDEN};
    lv_buttonmatrix_set_ctrl_map(buttonmatrix, buttonmatrix_ctrl_map_1);
}

void testing_on_power_button(void)
{
    power_button_clicked = true;
}

static void manageSleepState(void)
{
    if (power_button_clicked) {
        power_button_clicked = false;

        if (screen_asleep) {
            displayWake();
            screen_asleep = false;
        } else {
            displaySleep();
            screen_asleep = true;
        }

        last_activity_ms = nowMs();
    } else if (touched()) {
        /*Touch only wakes a sleeping screen / resets the idle timer - it must
          never put an already-awake screen to sleep, since a normal tap stays
          "touched" across many loop() iterations and would otherwise spam
          sleepDisplay()/wakeupDisplay() for the whole gesture.*/
        if (screen_asleep) {
            displayWake();
            screen_asleep = false;
        }

        last_activity_ms = nowMs();
    }

    if (!screen_asleep && (nowMs() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {
        lv_task_handler();

        displaySleep();
        screen_asleep = true;
    }
}

void setupGui(void)
{
    usable_area_init();

    //lv_example_canvas_rectangle();
    /*Pick one - both build a tileview on the active screen, so running both
      stacks them on top of each other.*/
    //lv_example_tileview_full_bleed();
    CreateButtonGrid();

    lv_task_handler();
    last_activity_ms = nowMs();
}

void loopGui(void)
{
    manageSleepState();
    lv_task_handler();
}
