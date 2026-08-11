#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <math.h>

#if defined(ARDUINO_T_WATCH_S3_ULTRA)

/**
 * @title Rounded-bezel safe area
 * @brief Lays widgets out inside the visible region of the T-Watch-Ultra's curved glass.
 *
 * The panel reports 410x502, but the cover glass is curved: anything outside a
 * ~120 px corner radius is hidden by the bezel. This sketch shows two ways to
 * account for that, side by side on one screen:
 *
 *   1. A fixed safe-area rectangle - the largest axis-aligned rect that fits
 *      inside the rounded viewport. Simple, but wastes the full-width space
 *      that is actually visible near the vertical middle.
 *
 *   2. Per-band insets - each horizontal band is widened to exactly the amount
 *      of width visible at its own vertical position, reclaiming that space.
 *
 * The bands are drawn so their ends trace the corner arcs. If BEZEL_RADIUS
 * matches the real glass, every band ends flush with the visible edge and none
 * are clipped. Tune BEZEL_RADIUS until that is true.
 */

/*The corner radius the curved glass hides everything outside of.*/
#define BEZEL_RADIUS    120

/*Largest axis-aligned rect inside a rounded rect: its corners land on the arc
  when the inset is r * (1 - 1/sqrt(2)). 36 px at r=120. Rounded up.*/
#define SAFE_INSET      ((int32_t)(BEZEL_RADIUS * 0.29289322f) + 1)

#define BAND_HEIGHT     28
#define BAND_GAP        6

static int32_t screen_w;
static int32_t screen_h;

/**
 * Horizontal inset required at vertical offset y (0 = top of the screen) for a
 * point to stay inside the rounded viewport. Zero between the two arcs, and
 * BEZEL_RADIUS at the very top and bottom edges.
 */
static int32_t bezel_inset_at(int32_t y)
{
    int32_t d;

    if (y < BEZEL_RADIUS) {
        d = BEZEL_RADIUS - y;                   /*inside the top arc*/
    } else if (y > screen_h - BEZEL_RADIUS) {
        d = y - (screen_h - BEZEL_RADIUS);      /*inside the bottom arc*/
    } else {
        return 0;                               /*straight section, full width*/
    }

    if (d > BEZEL_RADIUS) {
        d = BEZEL_RADIUS;                       /*off-screen y, clamp to the corner*/
    }

    return BEZEL_RADIUS - (int32_t)sqrtf((float)(BEZEL_RADIUS * BEZEL_RADIUS - d * d));
}

/**
 * Inset for a horizontal band spanning y_top..y_bot. A band has height, so the
 * binding constraint is whichever of its two edges sits deeper into a corner.
 */
static int32_t bezel_inset_for_band(int32_t y_top, int32_t y_bot)
{
    int32_t top = bezel_inset_at(y_top);
    int32_t bot = bezel_inset_at(y_bot);
    return top > bot ? top : bot;
}

/**
 * A container covering the fixed safe-area rectangle. Parent widgets to this
 * and the corners stop being a concern - at the cost of the reclaimable space.
 */
static lv_obj_t *safe_area_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, screen_w - 2 * SAFE_INSET, screen_h - 2 * SAFE_INSET);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

static void build_ui(void)
{
    screen_w = lv_display_get_horizontal_resolution(NULL);
    screen_h = lv_display_get_vertical_resolution(NULL);

    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /*Clip children to the rounded shape. Purely visual - it makes overflow
      invisible rather than preventing it, and does not affect hit testing.*/
    lv_obj_set_style_radius(root, BEZEL_RADIUS, 0);
    lv_obj_set_style_clip_corner(root, true, 0);

    /*Bands whose ends trace the corner arcs - approach 2.*/
    int32_t step  = BAND_HEIGHT + BAND_GAP;
    int32_t count = screen_h / step;
    int32_t y     = (screen_h - (count * step - BAND_GAP)) / 2;

    for (int32_t i = 0; i < count; i++, y += step) {
        int32_t inset = bezel_inset_for_band(y, y + BAND_HEIGHT - 1);
        int32_t w     = screen_w - 2 * inset;

        if (w <= 0) {
            continue;                           /*band lies entirely in the bezel*/
        }

        lv_obj_t *band = lv_obj_create(root);
        lv_obj_set_size(band, w, BAND_HEIGHT);
        lv_obj_set_pos(band, inset, y);
        lv_obj_set_style_radius(band, BAND_HEIGHT / 2, 0);
        lv_obj_set_style_border_width(band, 0, 0);
        lv_obj_set_style_pad_all(band, 0, 0);
        lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);

        /*Full-width bands are the space a fixed safe area would have thrown away.*/
        bool reclaimed = inset < SAFE_INSET;
        lv_obj_set_style_bg_color(band,
                                  reclaimed ? lv_palette_main(LV_PALETTE_TEAL)
                                            : lv_palette_darken(LV_PALETTE_BLUE_GREY, 2),
                                  0);
    }

    /*Outline of the fixed safe-area rect - approach 1, drawn over the bands.*/
    lv_obj_t *safe = safe_area_create(root);
    lv_obj_set_style_border_color(safe, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_border_width(safe, 2, 0);
    lv_obj_set_style_radius(safe, 0, 0);

    lv_obj_t *label = lv_label_create(safe);
    lv_label_set_text_fmt(label, "safe %dx%d\ninset %d px\nr %d",
                          (int)(screen_w - 2 * SAFE_INSET),
                          (int)(screen_h - 2 * SAFE_INSET),
                          (int)SAFE_INSET,
                          (int)BEZEL_RADIUS);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    build_ui();
}

void loop()
{
    instance.loop();
    lv_task_handler();
    delay(5);
}

#endif
