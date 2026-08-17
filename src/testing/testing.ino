#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <bosch/BoschSensorDataHelper.hpp>
#include <usable_area.h>


#if defined(ARDUINO_T_WATCH_S3_ULTRA)
#define CANVAS_WIDTH  410
#define CANVAS_HEIGHT  502
#define spacing 0

// How long the screen stays on with no activity before it sleeps.
#define SCREEN_SLEEP_TIMEOUT_MS  10000

/**
 * @title Rectangle with border and outline
 * @brief Draw a red rounded rectangle with a blue border and a green outline onto a canvas.
 *
 * A 50x50 `LV_COLOR_FORMAT_ARGB8888` canvas is centered and filled with
 * a light grey background. An `lv_draw_rect_dsc_t` is populated with a
 * red fill, radius 5, a 3 px blue border, and a 2 px green outline at
 * `LV_OPA_50`, then painted into area {10,10,40,30} via `lv_draw_rect`
 * on a layer opened with `lv_canvas_init_layer`.
 */

bool power_button_clicked = false;
bool screen_asleep = false;
uint32_t last_activity_ms = 0;
lv_obj_t *label1;
static SensorOrientation orientation(instance.sensor);
uint32_t previousOrientation = 0;
/// BHI260 device-orientation codes, per the BHY2 sensor API.
static const char *orientation_name(uint32_t o)
{
    switch (o) {
    case 0:  return "portrait upright";
    case 1:  return "landscape left";
    case 2:  return "portrait upside down";
    case 3:  return "landscape right";
    default: return "unknown";
    }
}
void lv_example_canvas_rectangle(void)
{
    /*Create a buffer for the canvas. At full screen size this is 410*502*4 = 823 kB,
      far more than the ESP32-S3's 320 kB of internal DRAM, so it has to come from PSRAM.*/
    static uint8_t *canvas_buf = NULL;
    if (!canvas_buf) {
        canvas_buf = (uint8_t *)ps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * 4);
        if (!canvas_buf) {
            Serial.println("Failed to allocate canvas buffer");
            return;
        }
    }

    /*Create a canvas and initialize its palette*/
    lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);

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
    dsc.border_width = 3;

    /*lv_area_t is {x1, y1, x2, y2} - top-left and bottom-right corners,
      so x2 must be > x1 and y2 must be > y1 or nothing is drawn.*/
    lv_area_t coords = {spacing, spacing, CANVAS_WIDTH - spacing, CANVAS_HEIGHT - spacing};

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
void ManageSleepState() {
    bool touched = instance.getTouched();

    if (power_button_clicked) {
        power_button_clicked = false;

        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;
        } else {
            instance.sleepDisplay();
            screen_asleep = true;
        }

        last_activity_ms = millis();
    } else if (touched) {
        /*Touch only wakes a sleeping screen / resets the idle timer - it must
          never put an already-awake screen to sleep, since a normal tap stays
          "touched" across many loop() iterations and would otherwise spam
          sleepDisplay()/wakeupDisplay() for the whole gesture.*/
        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;
        }

        last_activity_ms = millis();
    }

    if (!screen_asleep && (millis() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {
        //lv_label_set_text(label1, "Sleeping (timeout) - touch screen or click power button to wake");
        lv_task_handler();

        instance.sleepDisplay();
        screen_asleep = true;
    }
}
void CreateButtonGrid(void) {
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
    static const lv_buttonmatrix_ctrl_t buttonmatrix_ctrl_map_1[] = {LV_BUTTONMATRIX_CTRL_NONE, (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CHECKED), LV_BUTTONMATRIX_CTRL_DISABLED, LV_BUTTONMATRIX_CTRL_HIDDEN};
    lv_buttonmatrix_set_ctrl_map(buttonmatrix, buttonmatrix_ctrl_map_1);

 }
void setup()
{
    Serial.begin(115200);

    instance.begin();

    /*The CST9217 touch chip's raw axes don't match the panel's mounted
      orientation on this unit, and LilyGoWatchUltra::initTouch() never
      corrects for it (no setSwapXY/setMirrorXY call), so every swipe comes
      in reversed. setMaxCoordinates() must come first: the mirror math in
      TouchDrvInterface::updateXY() is `_xMax - x` / `_yMax - y`, gated on
      _xMax/_yMax being nonzero - they default to 0 and nothing else in this
      codebase sets them, so setMirrorXY() alone would silently no-op.*/
    //instance.touch.setMaxCoordinates(CANVAS_WIDTH, CANVAS_HEIGHT);
    //instance.touch.setMirrorXY(true, true);

    beginLvglHelper(instance);
    usable_area_init();
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    //lv_example_canvas_rectangle();
    /*Pick one - both build a tileview on the active screen, so running both
      stacks them on top of each other.*/
    //lv_example_tileview_l_shape();
    CreateButtonGrid();
    //lv_example_tileview_full_bleed();
    // label1 = lv_label_create(lv_screen_active());
    // lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL);
    // lv_obj_set_width(label1, LV_PCT(90));
    // lv_label_set_text(label1, "Awake");
    // lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 10);

    lv_task_handler();
    previousOrientation = 0;
    instance.onEvent([](DeviceEvent_t event, void *params, void * user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            power_button_clicked = true;
        }
    }, POWER_EVENT, NULL);
    float sample_rate = 5.0;
    uint32_t report_latency_ms = 0;
    orientation.enable(sample_rate, report_latency_ms);
    last_activity_ms = millis();
}

void loop()
{
    instance.loop();

    ManageSleepState();
    static uint32_t last = 0;

    if (millis() - last > 500) {
        last = millis();
        uint32_t o = orientation.getOrientation();
        Serial.printf("orientation: %lu (%s)\n", (unsigned long)o, orientation_name(o));

        if (previousOrientation != o) {
        Serial.printf("previous orientation: %lu (%s)\n", (unsigned long)o, orientation_name(previousOrientation));
            if (previousOrientation == 0 && o ==2) {
                if (screen_asleep) {
                    instance.wakeupDisplay();
                    screen_asleep = false;
                    // lv_label_set_text(label1, "Awake (woken by orientation change)");
                } else {
                    // lv_label_set_text(label1, "Orientation changed to landscape left");
                }
            }
            
        }

        previousOrientation = o;
    }
    lv_task_handler();
    delay(5);
}
#endif
