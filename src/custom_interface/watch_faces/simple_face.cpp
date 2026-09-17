/**
 * @file      simple_face.cpp
 * @license   MIT
 * @brief     The digital watch face. See simple_face.h.
 *
 * Ported from src/factory's setupClock() (ui_main.cpp), the clock this repo's
 * factory firmware drops to when the launcher goes idle: the hour and the
 * minute in their own translucent panels either side of a blinking colon, the
 * date and a battery meter along the bottom, all set in Alibaba PuHuiTi Bold.
 *
 * Two things change in the move here.
 *
 * The original places everything against the raw panel, in absolute pixels
 * chosen by branching on the display resolution (three sizes, two font cuts).
 * This app is Ultra-only and has a curved bezel to lay out around, so the face
 * is built inside usable_area_rect() and sized in percentages of that rect --
 * see app_config.h's APP_FACE_* block for every number below.
 *
 * The original also draws over a full-screen wallpaper (img_background2) and
 * lets the LVGL theme colour the panels on top of it. There is no wallpaper
 * here, so the panels state their own colours rather than inheriting a theme
 * fill that would be invisible against black.
 *
 * Time and battery come from the RTC and PMU rather than millis(), so the face
 * stays correct across sleep.
 */

#include "simple_face.h"

#include "../app_config.h"

#include <usable_area.h>

#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#else
#include <stdlib.h>
#include <time.h>
#endif

/// The face's own root inside the screen, so *_deinit() can delete the whole
/// face by deleting one object rather than tracking every widget on it.
static lv_obj_t *face_root;
static lv_timer_t *face_timer;

static lv_obj_t *label_hour;
static lv_obj_t *label_minute;
static lv_obj_t *label_colon;
static lv_obj_t *label_date;
static lv_obj_t *label_batt;
static lv_obj_t *bar_batt;

static const char *weekday_name(uint8_t w)
{
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[w > 6 ? 0 : w];
}

/// One of the two panels the hour/minute sits in. Identical apart from which
/// edge it hugs, so they are built once here rather than twice inline.
static lv_obj_t *make_clock_panel(lv_obj_t *parent, lv_align_t align, int32_t x_ofs)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(APP_FACE_CLOCK_BOX_WIDTH_PCT),
                    LV_PCT(APP_FACE_CLOCK_BOX_HEIGHT_PCT));
    lv_obj_align(panel, align, x_ofs, APP_FACE_CLOCK_Y_OFFSET);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Factory takes the fill and border colours from the theme because it has
    // a photo behind them; on black, a themed dark fill at 20% is not there at
    // all. White at the same opacities gives the frosted-panel look the
    // original has over its wallpaper.
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, LV_PART_MAIN);
    return panel;
}

/// The hour or minute digits, centred in their panel.
static lv_obj_t *make_clock_label(lv_obj_t *panel, const char *initial)
{
    lv_obj_t *label = lv_label_create(panel);
    lv_obj_set_style_text_font(label, APP_FONT_FACE_TIME, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    // APP_FONT_FACE_TIME carries digits and `:` only, so the placeholder is
    // digits -- the "--:--" a smaller font could show has no glyphs here. It
    // is overwritten before the first frame anyway; see simple_face_init().
    lv_label_set_text(label, initial);
    lv_obj_center(label);
    return label;
}

static void build_face(lv_obj_t *screen)
{
    // usable_area_style_screen() already painted/clipped the screen itself;
    // build the face inside the largest rect that's safe everywhere under
    // the curved bezel rather than against the screen's raw bounds.
    lv_obj_t *scr = usable_area_rect(screen);
    face_root = scr;

    lv_obj_t *hour_panel = make_clock_panel(scr, LV_ALIGN_LEFT_MID, APP_FACE_CLOCK_X_OFFSET);
    lv_obj_t *min_panel  = make_clock_panel(scr, LV_ALIGN_RIGHT_MID, -APP_FACE_CLOCK_X_OFFSET);

    label_hour   = make_clock_label(hour_panel, "12");
    label_minute = make_clock_label(min_panel, "34");

    // The colon is parented to the rect, not to either panel, so it sits in
    // the gap between them. It blinks -- refresh() toggles it once a second --
    // which is the face's only indication that the clock is still running.
    label_colon = lv_label_create(scr);
    lv_obj_set_style_text_font(label_colon, APP_FONT_FACE_TIME, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_colon, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(label_colon, ":");
    lv_obj_align(label_colon, LV_ALIGN_CENTER, 0, -10 + APP_FACE_CLOCK_Y_OFFSET);

    label_date = lv_label_create(scr);
    lv_obj_set_style_text_font(label_date, APP_FONT_FACE_DATE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_date, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(label_date, "03-24 Mon");
    lv_obj_align(label_date, LV_ALIGN_BOTTOM_MID, 0, APP_FACE_BOTTOM_OFFSET);

    // The meter is an lv_bar *inside* the battery outline image, so the image
    // is the shell and the bar is the charge in it -- one widget's worth of
    // drawing for both.
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &img_battery);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, APP_FACE_BATT_TOP_OFFSET);
    bar_batt = lv_bar_create(img);
    lv_obj_set_size(bar_batt, img_battery.header.w - APP_FACE_BATT_BAR_INSET_W,
                    img_battery.header.h - APP_FACE_BATT_BAR_INSET_H);
    lv_bar_set_range(bar_batt, 0, 100);
    lv_bar_set_value(bar_batt, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_batt, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_batt, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_batt, lv_color_make(0, 255, 0), LV_PART_INDICATOR);
    // One px left of centre: the outline's terminal nub is on its right, so
    // centring on the whole image would push the fill under it.
    lv_obj_align(bar_batt, LV_ALIGN_CENTER, -1, 0);

    label_batt = lv_label_create(scr);
    lv_obj_set_style_text_font(label_batt, APP_FONT_FACE_BATT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_batt, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(label_batt, "100%");

    // A rule across the bottom, above the date.
    static const lv_point_precise_t divider_points[] = {
        {0, 0},
        {APP_FACE_DIVIDER_WIDTH, 0},
    };
    lv_obj_t *divider = lv_line_create(scr);
    lv_line_set_points(divider, divider_points, 2);
    lv_obj_set_style_line_width(divider, APP_FACE_DIVIDER_THICKNESS, LV_PART_MAIN);
    lv_obj_set_style_line_color(divider, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_opa(divider, LV_OPA_60, LV_PART_MAIN);

    // The last two are placed relative to widgets rather than to an edge, and
    // lv_obj_align_to() reads the reference's cached coords -- which are still
    // 0x0 until the layout it was just marked dirty by actually runs. Same
    // trap batman_dial.cpp documents around lv_obj_get_width().
    lv_obj_align_to(label_batt, img, LV_ALIGN_OUT_RIGHT_MID, APP_FACE_BATT_LABEL_GAP, 0);
    lv_obj_align_to(divider, label_date, LV_ALIGN_OUT_TOP_MID, 0, -APP_FACE_DIVIDER_GAP);
    lv_obj_update_layout(scr);
}

static void refresh(lv_timer_t *t)
{
    LV_UNUSED(t);

#ifdef ARDUINO
    RTC_DateTime now = instance.rtc.getDateTime();
    uint8_t hour = now.getHour();
    uint8_t minute = now.getMinute();
    // getWeek() is the weekday (0-6); getDay() is the day of month -- see
    // SensorRTC.h's own RTC_DateTime::printDatetime() for the same pairing.
    uint8_t week = now.getWeek();
    uint8_t month = now.getMonth();
    uint8_t day = now.getDay();

    int percent = instance.pmu.getBatteryPercent();
    if (percent < 0) percent = 0;
    bool charging = instance.pmu.isCharging();
#else
    // No RTC/PMU on the host -- the wall clock stands in for the one, a
    // fixed/jittered reading for the other. Same shape as
    // src/factory/hal_interface.cpp's native stub (30 + rand() % 71).
    time_t raw = time(NULL);
    struct tm *lt = localtime(&raw);
    uint8_t hour = lt->tm_hour;
    uint8_t minute = lt->tm_min;
    uint8_t week = lt->tm_wday;
    uint8_t month = lt->tm_mon + 1;
    uint8_t day = lt->tm_mday;

    int percent = 30 + rand() % 71;
    bool charging = false;
#endif

    // Blink, by hiding and showing on alternate ticks. The timer runs at 1 Hz
    // so this is a one-second cycle, not the half-second a seconds-hand blink
    // would be -- matching factory.
    lv_obj_has_flag(label_colon, LV_OBJ_FLAG_HIDDEN) ?
    lv_obj_remove_flag(label_colon, LV_OBJ_FLAG_HIDDEN) :
    lv_obj_add_flag(label_colon, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(label_hour, "%02u", hour);
    lv_label_set_text_fmt(label_minute, "%02u", minute);
    lv_label_set_text_fmt(label_date, "%02u-%02u %s", month, day, weekday_name(week));

    lv_bar_set_value(bar_batt, percent, LV_ANIM_OFF);
    // A leading "+" for charging: factory shows the percentage alone, and
    // LV_SYMBOL_CHARGE would need a font carrying LVGL's symbol range, which
    // APP_FONT_FACE_BATT (ASCII 32-127) does not.
    lv_label_set_text_fmt(label_batt, "%s%d%%", charging ? "+" : "", percent);
}

void simple_face_init(lv_obj_t *screen)
{
    build_face(screen);

    // One timer at 1 Hz, which is what the blinking colon needs; the digits
    // only change once a minute. Kept in a static so simple_face_deinit() can
    // stop it; a face that is torn down and rebuilt would otherwise accumulate
    // one timer per switch, each still writing to labels that no longer exist.
    face_timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(face_timer); // paint immediately rather than after 1s
}

void simple_face_deinit(void)
{
    // Timer first: deleting the widgets while it is still armed leaves one
    // tick able to run against freed labels.
    if (face_timer) {
        lv_timer_delete(face_timer);
        face_timer = NULL;
    }
    if (face_root) {
        lv_obj_delete(face_root);
        face_root = NULL;
    }
    label_hour = label_minute = label_colon = NULL;
    label_date = label_batt = bar_batt = NULL;
}
