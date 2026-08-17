/**
 * @file      batman_dial.cpp
 * @license   MIT
 * @brief     Analog watch face with drawn hands. See batman_dial.h.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/BatmanDial (by way of
 * examples/ui/BatmanDial/BatmanDial.ino).
 *
 * The original shipped a bitmap dial and rotated sprite hands through
 * TFT_eSPI. There is no TFT_eSPI here and no bitmap to reuse, so the dial is
 * drawn with LVGL primitives instead: an lv_line per hand, repositioned each
 * second from the RTC.
 *
 * Trigonometry note -- screen Y grows downward and 0 degrees points right,
 * so the angle is offset by -90 degrees to put 12 o'clock at the top, and
 * the sine term is subtracted rather than added.
 *
 * Built inside usable_area_rect(screen) rather than against the screen's raw
 * bounds, so the dial and digital readout stay clear of the curved bezel;
 * the dial radius is derived from that area's own size (not the full
 * screen), so it still scales to either watch.
 */

#include "batman_dial.h"

#include <math.h>

#include <usable_area.h>

#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#else
#include <stdlib.h>
#include <time.h>
#endif

static lv_obj_t *hand_hour;
static lv_obj_t *hand_min;
static lv_obj_t *hand_sec;

/// Battery level, 0-100. The arc and its readout both observe this, so
/// refresh() sets one integer instead of touching either widget.
static lv_subject_t batt_subject;

static lv_point_precise_t pts_hour[2];
static lv_point_precise_t pts_min[2];
static lv_point_precise_t pts_sec[2];

static int32_t centre_x, centre_y, radius, arc_spacing;

static lv_obj_t *make_hand(lv_obj_t *parent, lv_color_t colour, int32_t width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_color(line, colour, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

/// Point a hand at `deg` degrees clockwise from 12, `len` pixels long.
static void set_hand(lv_obj_t *line, lv_point_precise_t *pts, float deg, int32_t len)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;

    pts[0].x = centre_x;
    pts[0].y = centre_y;
    pts[1].x = centre_x + (int32_t)(cosf(rad) * len);
    pts[1].y = centre_y + (int32_t)(sinf(rad) * len);

    lv_line_set_points(line, pts, 2);
}

static void refresh(lv_timer_t *t)
{
    LV_UNUSED(t);

#ifdef ARDUINO
    RTC_DateTime now = instance.rtc.getDateTime();
    uint8_t hour   = now.getHour();
    uint8_t minute = now.getMinute();
    uint8_t second = now.getSecond();

    int percent = instance.pmu.getBatteryPercent();
    if (percent < 0) percent = 0;
#else
    // No RTC or PMU on the host -- the wall clock stands in for the one, a
    // jittered reading for the other, the same substitutions
    // simple_face.cpp makes.
    time_t raw = time(NULL);
    struct tm *lt = localtime(&raw);
    uint8_t hour   = lt->tm_hour;
    uint8_t minute = lt->tm_min;
    uint8_t second = lt->tm_sec;

    int percent = 30 + rand() % 71;
#endif

    // Hour hand advances smoothly with the minutes, as a real movement does.
    float hour_deg = ((hour % 12) + minute / 60.0f) * 30.0f;
    float min_deg  = minute * 6.0f;
    float sec_deg  = second * 6.0f;

    set_hand(hand_hour, pts_hour, hour_deg, radius / 2);
    set_hand(hand_min,  pts_min,  min_deg,  (radius * 3) / 4);
    set_hand(hand_sec,  pts_sec,  sec_deg,  (radius * 9) / 10);

    // One write, two widgets: the arc and its readout are both observers.
    lv_subject_set_int(&batt_subject, percent);
}
/// An arc spanning `start_deg`..`end_deg` on a circle of radius `r`, centred
/// on the parent. LVGL puts 0 deg at 3 o'clock and grows clockwise, so the
/// top-right quadrant is 270..360.
///
/// No lv_arc_set_angles() here: the indicator angles are derived from the
/// bound value, and setting them explicitly would just be overwritten on the
/// first update.
static lv_obj_t *make_arc(lv_obj_t *parent, lv_color_t colour, int32_t width,
                          int32_t r, int32_t start_deg, int32_t end_deg)
{
    // lv_arc's radius is its OUTER edge: get_center() takes min(w,h)/2 and the
    // renderer insets the inner circle by arc_width, so sizing the object to
    // r*2 puts the ring at [r - width, r]. Padding would shrink it further.
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, r * 2, r * 2);
    lv_obj_center(arc);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_arc_set_bg_angles(arc, start_deg, end_deg);

    // Indicative only, the way lv_example_arc_bind_value locks its arc --
    // except 9.2.2 has no lv_obj_set_clickable(), so remove the flag directly.
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, colour, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

static void build_face(lv_obj_t *screen)
{
    // usable_area_style_screen() already painted/clipped the screen itself;
    // build the dial inside the largest rect that's safe everywhere under
    // the curved bezel rather than against the screen's raw bounds.
    //lv_obj_t *area = usable_area_rect(screen);
    lv_obj_t *area = screen;

    // usable_area_rect() sets the size as a style, which only marks the layout
    // dirty -- lv_obj_get_width() reads the cached coords, so without this it
    // reads back 0x0 (the coords a freshly created object starts with) and the
    // whole dial collapses to a negative radius at the top-left corner.
    lv_obj_update_layout(area);
    int32_t w = lv_obj_get_width(area);
    int32_t h = lv_obj_get_height(area);
    if (w <= 0 || h <= 0) {
        // Layout still has not settled (should not normally happen) -- fall
        // back to a small, visible dial rather than a negative one.
        w = h = 120;
    }
    centre_x = w / 2;
    centre_y = h / 2;
    radius   = (w < h ? w : h) / 2 - 10;
   
    if (radius < 10) {
        radius = 10;
    }

    // Dial face. This MUST be lv_scale_create(), not lv_obj_create(): the
    // lv_scale_set_*() calls below cast the pointer to lv_scale_t*, which is
    // an lv_obj_t followed by ~60 bytes of its own fields. A plain object
    // allocates only the lv_obj_t, so those setters write off the end of the
    // heap block -- panic and reboot loop on hardware, malloc corruption
    // natively. Nothing catches the mismatch: every widget setter takes
    // lv_obj_t*, and LV_USE_ASSERT_OBJ is 0 in both lv_conf paths.
    lv_obj_t *dial = lv_scale_create(area);
    lv_obj_set_size(dial, (radius * 2)-arc_spacing, (radius * 2)-arc_spacing);
    lv_obj_center(dial);
    lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);

    // 60 minute ticks around a full circle. Tick n sits at
    // n * angle_range / (total_tick_count - 1) degrees, so 61 ticks over 360
    // gives exactly 6 deg spacing -- with 60 the spacing becomes 360/59 and
    // the ring visibly fails to close. Ticks 0 and 60 land on the same spot
    // at 12 o'clock and are both major, so the overlap is invisible.
    // rotation 270 puts tick 0 at the top (LVGL measures 0 deg from 3
    // o'clock). The range only feeds the labels, which are off here.
    lv_scale_set_mode(dial, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(dial, 61);
    lv_scale_set_major_tick_every(dial, 5);
    lv_scale_set_angle_range(dial, 360);
    lv_scale_set_rotation(dial, 270);
    lv_scale_set_range(dial, 0, 60);
    // Hour numerals on the major ticks. lv_scale_set_text_src() keeps the
    // pointer and walks the array until a NULL to count the labels, so it has
    // to be static and NULL-terminated -- a braced literal or a local array
    // would leave the scale reading dead memory on every redraw.
    //
    // Twelve entries for thirteen major ticks: the last one is tick 60, which
    // sits exactly on tick 0, so leaving it unlabelled avoids drawing "12"
    // twice over itself (which would blend the antialiased edges and make it
    // look heavier than the other numerals).
    static const char *hour_labels[] = {
        "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL
    };
    lv_scale_set_text_src(dial, hour_labels);
    lv_scale_set_label_show(dial, true);

    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dial, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dial, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_border_width(dial, 3, 0);

    // LV_PART_ITEMS is the minor ticks, LV_PART_INDICATOR the major ones --
    // and the numerals too, which take their text style from the same part.
    lv_obj_set_style_length(dial, 10, LV_PART_ITEMS);
    lv_obj_set_style_line_width(dial, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(dial, lv_palette_main(LV_PALETTE_GREY), LV_PART_ITEMS);
    lv_obj_set_style_length(dial, 15, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(dial, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(dial, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(dial, lv_color_white(), LV_PART_INDICATOR);
    // There is no lv_scale_set_style() in 9.2.2 -- the scale's only setters are
    // the twelve lv_scale_set_* in lv_scale.h. A local style on the part the
    // labels draw from does the same job.
    lv_obj_set_style_text_font(dial, &lv_font_montserrat_28, LV_PART_INDICATOR);

    // Battery arc in the top-right quadrant, sitting just outside the dial,
    // with a straight readout inside it. Both bind to batt_subject rather than
    // being written directly, so refresh() updates one integer and the two
    // widgets follow -- the pattern from lv_example_arc_bind_value.
    

    // Computed, not measured: the lv_obj_set_size() above only marks the
    // layout dirty, so lv_obj_get_width(dial) would still read back 0 here.
    int32_t dial_r = ((radius * 2) - arc_spacing) / 2;
    int32_t arc_r  = dial_r + ARC_GAP + ARC_WIDTH;

    lv_subject_init_int(&batt_subject, 0);

    lv_obj_t *batt_arc = make_arc(area, lv_palette_main(LV_PALETTE_YELLOW),
                                  ARC_WIDTH, arc_r, 270, 360);
    // 9.2.2 has no lv_subject_set_min/max_value_int -- the observer callback
    // just calls lv_arc_set_value(), which clamps to the widget's own range,
    // so the limits live here.
    lv_arc_set_range(batt_arc, 0, 100);
    lv_arc_bind_value(batt_arc, &batt_subject);

    // Readout at the quadrant's midpoint, upright. The dial, the arc and this
    // label all share the parent's centre, so a plain polar offset from
    // LV_ALIGN_CENTER lands it on the ring -- and LV_ALIGN_CENTER positions
    // the label by its own centre, so no half-text-width correction.
    //
    // Outside the ring: arc_r is the arc's OUTER edge (the renderer insets
    // inward by ARC_WIDTH, so the ring occupies [arc_r - ARC_WIDTH, arc_r]),
    // meaning anything past arc_r clears it. Half the line height puts the
    // label's inner edge -- not its centre -- ARC_GAP px off the arc, and
    // derives from the font so changing it does not need a new magic number.
    const lv_font_t *readout_font = &lv_font_montserrat_20;
    const float mid_rad = 315.0f * (float)M_PI / 180.0f;
    int32_t label_r = arc_r + ARC_GAP + lv_font_get_line_height(readout_font) / 2;

    lv_obj_t *readout = lv_label_create(area);
    lv_obj_set_style_text_font(readout, readout_font, 0);
    lv_obj_set_style_text_color(readout, lv_color_white(), 0);
    lv_label_bind_text(readout, &batt_subject, "%d%%");
    lv_obj_align(readout, LV_ALIGN_CENTER,
                 (int32_t)(cosf(mid_rad) * label_r),
                 (int32_t)(sinf(mid_rad) * label_r));

    hand_hour = make_hand(area, HOUR_HAND_COLOR, HOUR_HAND_WIDTH);
    hand_min  = make_hand(area, MIN_HAND_COLOR, MIN_HAND_WIDTH);
    hand_sec  = make_hand(area, SEC_HAND_COLOR, SEC_HAND_WIDTH);

    // label_digital = lv_label_create(area);
    // lv_obj_set_style_text_color(label_digital, lv_palette_main(LV_PALETTE_GREY), 0);
    // lv_obj_align(label_digital, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void batman_dial_init(lv_obj_t *screen)
{
    build_face(screen);

    // One timer at 1 Hz is enough -- the second hand only needs to jump once
    // a second, not redraw continuously.
    lv_timer_t *timer = lv_timer_create(refresh, 1000, NULL);
    lv_timer_ready(timer);      // paint immediately rather than after 1s
}
