# 1 "/tmp/tmp2m86hbdm"
#include <Arduino.h>
# 1 "/workspaces/PlatformIO-Snvgglebear/src/testing_safe_area/safe_area.ino"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include "safe_area.h"

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
# 19 "/workspaces/PlatformIO-Snvgglebear/src/testing_safe_area/safe_area.ino"
#define BAND_HEIGHT 28
#define BAND_GAP 6
#define SCREEN_SLEEP_TIMEOUT_MS 10000

bool power_button_clicked = false;
bool screen_asleep = false;
uint32_t last_activity_ms = 0;
static void build_ui(void);
void setup();
void ManageSleepState();
void loop();
#line 27 "/workspaces/PlatformIO-Snvgglebear/src/testing_safe_area/safe_area.ino"
static void build_ui(void)
{
    int32_t screen_w = safe_area_screen_width();
    int32_t screen_h = safe_area_screen_height();


    int32_t step = BAND_HEIGHT + BAND_GAP;
    int32_t count = screen_h / step;
    int32_t y = (screen_h - (count * step - BAND_GAP)) / 2;

    for (int32_t i = 0; i < count; i++, y += step) {
        lv_obj_t *band = safe_area_place(lv_screen_active(), y, BAND_HEIGHT);
        if (!band) {
            continue;
        }

        lv_obj_set_style_radius(band, BAND_HEIGHT / 2, 0);
        lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);


        bool reclaimed = safe_area_inset_for_band(y, y + BAND_HEIGHT - 1) < SAFE_INSET;
        lv_obj_set_style_bg_color(band,
                                  reclaimed ? lv_palette_main(LV_PALETTE_TEAL)
                                            : lv_palette_darken(LV_PALETTE_BLUE_GREY, 2),
                                  0);
    }


    lv_obj_t *safe = safe_area_rect(lv_screen_active());
    lv_obj_set_style_border_color(safe, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_border_width(safe, 2, 0);
    lv_obj_set_style_radius(safe, 0, 0);

    lv_obj_t *info = lv_label_create(safe);
    lv_label_set_text_fmt(info, "safe %dx%d\ninset %d px\nr %d",
                          (int)(screen_w - 2 * SAFE_INSET),
                          (int)(screen_h - 2 * SAFE_INSET),
                          (int)SAFE_INSET,
                          (int)BEZEL_RADIUS);
    lv_obj_set_style_text_color(info, lv_color_white(), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(info);



    lv_obj_t *btn_area = safe_area_place(lv_screen_active(), 6, 40);
    if (btn_area) {
        lv_obj_t *btn = lv_button_create(btn_area);
        lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, "tap me");
        lv_obj_center(btn_label);
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    safe_area_init();

    build_ui();
    instance.onEvent([](DeviceEvent_t event, void *params, void * user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            power_button_clicked = true;
        }
    }, POWER_EVENT, NULL);
}
void ManageSleepState() {
bool touched = instance.getTouched();

    if (power_button_clicked || touched) {
        power_button_clicked = false;

        if (screen_asleep) {
            instance.wakeupDisplay();
            screen_asleep = false;

        } else {
            instance.sleepDisplay();
            screen_asleep = true;
        }

        last_activity_ms = millis();
    }

    if (!screen_asleep && (millis() - last_activity_ms >= SCREEN_SLEEP_TIMEOUT_MS)) {

        lv_task_handler();

        instance.sleepDisplay();
        screen_asleep = true;
    }
}
void loop()
{

    instance.loop();
    ManageSleepState();
    lv_task_handler();
    delay(5);
}

#endif