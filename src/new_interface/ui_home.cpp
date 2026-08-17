/**
 * @file      ui_home.cpp
 * @license   MIT
 * @brief     Home screen: battery status, pinned links.
 *
 * Composes the two home-screen pieces inside menu_panel, partitioning its
 * safe-area-visible height top to bottom: battery band, pinned-links band.
 * Each band is handed to usable_area_place() so nothing renders under the
 * T-Watch-Ultra's curved bezel.
 *
 * The clock used to live here too (the top 55% of this same screen); it now
 * has its own tile, ui_clockface.cpp -- see that file's header comment for
 * why ("digital clock is smushed together" bugfix,
 * src/custom_interface/plan.md's interface_bugfixes section).
 */
#include "ui_home.h"
#include "ui_define.h"
#include "app_config.h"
#include "ui_battery_status.h"
#include "ui_pinned_links.h"
#include <usable_area.h>

void ui_home_build(lv_obj_t *parent)
{
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    int32_t screen_h = usable_area_screen_height();

    int32_t battery_h = (screen_h * 20) / 100;
    int32_t pinned_h  = screen_h - battery_h;

    lv_obj_t *battery_area = usable_area_place(parent, 0, battery_h);
    if (battery_area) {
        ui_battery_status_create(battery_area);
    }

    lv_obj_t *pinned_area = usable_area_place(parent, battery_h, pinned_h);
    if (pinned_area) {
        ui_pinned_links_build(pinned_area);
    }
}
