/**
 * @file      TouchPad.ino
 * @license   MIT
 * @brief     Read raw touch coordinates without going through LVGL.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/TouchPad.
 *
 * LilyGoLib ships no touch example at all -- touch is normally consumed by LVGL
 * via beginLvglHelper(), and the application never sees a coordinate. This shows
 * the layer underneath: instance.getTouched() reports whether a finger is down,
 * and instance.getPoint() fills coordinate arrays.
 *
 * Useful when you want a gesture the LVGL indev does not model, or when driving
 * a non-LVGL screen. LVGL is still initialised here purely to draw the readout.
 *
 * Note both this sketch and LVGL poll the same touch controller. That is fine
 * for a demo, but in real code pick one owner for the panel.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef USING_INPUT_DEV_TOUCHPAD

static lv_obj_t *label1;
static lv_obj_t *marker;
static uint32_t press_count = 0;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(label1, "Touch the screen");

    // A dot that follows the finger, so the coordinates can be sanity-checked
    // visually as well as read off the serial port.
    marker = lv_obj_create(lv_scr_act());
    lv_obj_set_size(marker, 24, 24);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

    if (!instance.hasTouch()) {
        lv_label_set_text(label1, "No touch controller detected");
    }
}

void loop()
{
    if (instance.getTouched()) {
        int16_t x[2] = {0};
        int16_t y[2] = {0};

        // Ask for up to two contacts; the return value says how many are real.
        uint8_t points = instance.getPoint(x, y, 2);

        if (points > 0) {
            press_count++;

            lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
            // LVGL positions from the top-left corner, so offset by half the dot.
            lv_obj_set_pos(marker, x[0] - 12, y[0] - 12);

            if (points > 1) {
                lv_label_set_text_fmt(label1,
                                      "points: %u\nP0  X:%d  Y:%d\nP1  X:%d  Y:%d",
                                      points, x[0], y[0], x[1], y[1]);
            } else {
                lv_label_set_text_fmt(label1, "points: %u\nX:%d  Y:%d", points, x[0], y[0]);
            }

            Serial.printf("touch %lu: points=%u x=%d y=%d\n", press_count, points, x[0], y[0]);
        }
    } else {
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }

    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support boards with a touch panel"); delay(1000);
}

#endif
