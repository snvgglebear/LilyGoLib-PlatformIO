#include "usable_area/usable_area.h"
void setup() {
  // Initialize the usable area
  safe_area_init();
  lv_obj_t *btn_area = safe_area_place(lv_screen_active(), 6, 40);
    if (btn_area) {
        lv_obj_t *btn = lv_button_create(btn_area);
        lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, "tap me");
        lv_obj_center(btn_label);
    }
}
void loop() {
  // Your main loop code here
}
