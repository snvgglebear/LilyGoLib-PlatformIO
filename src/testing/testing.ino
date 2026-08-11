#include <LilyGoLib.h>
#include <LV_Helper.h>


#if defined(ARDUINO_T_WATCH_S3_ULTRA)
#define CANVAS_WIDTH  410
#define CANVAS_HEIGHT  502
#define spacing 0

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

RTC_DATA_ATTR int bootCount = 0;

bool  power_button_clicked = false;
lv_obj_t *label1;


const char *get_wakeup_reason()
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0 : return ("Wakeup caused by external signal using RTC_IO");
    case ESP_SLEEP_WAKEUP_EXT1 : return ("Wakeup caused by external signal using RTC_CNTL");
    case ESP_SLEEP_WAKEUP_TIMER : return ("Wakeup caused by timer");
    case ESP_SLEEP_WAKEUP_TOUCHPAD : return ("Wakeup caused by touchpad");
    case ESP_SLEEP_WAKEUP_ULP : return ("Wakeup caused by ULP program");
    default : return ("Wakeup was not caused");
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
void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_example_canvas_rectangle();
    lv_task_handler();
    instance.onEvent([](DeviceEvent_t event, void *params, void * user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            power_button_clicked = true;
        }
    }, POWER_EVENT, NULL);

    // Waiting to press the crown to go to sleep
    while (!power_button_clicked) {
        // Handle device event
        instance.loop();
        // Handle lvgl event
        lv_task_handler();
        delay(5);
    }

    for (int i = 5; i > 0; i--) {
        lv_label_set_text_fmt(label1, "Go to sleep after %d seconds", i);
        lv_task_handler();
        delay(1000);
    }

    lv_label_set_text(label1, "Sleep now ...");
    lv_task_handler();
    delay(1000);

    /*
    * Wake up by touch panel
    * T-Watch-S3 deep sleep is about 1.08 mA
    * T-Watch-S3-Ultra deep sleep is about 3.34 mA
    * * */
    instance.sleep(WAKEUP_SRC_TOUCH_PANEL);
    lv_task_handler();
}
void loop()
{
    // lv_task_handler();
    // delay(5);
}
#endif