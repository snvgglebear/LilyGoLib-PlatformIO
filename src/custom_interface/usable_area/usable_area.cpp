#include "usable_area.h"
#include <math.h>

static int32_t screen_w;
static int32_t screen_h;

void usable_area_init(void)
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
}

int32_t safe_area_screen_width(void)
{
    return screen_w;
}

int32_t safe_area_screen_height(void)
{
    return screen_h;
}

int32_t safe_area_inset_at(int32_t y)
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

int32_t safe_area_inset_for_band(int32_t y_top, int32_t y_bot)
{
    int32_t top = safe_area_inset_at(y_top);
    int32_t bot = safe_area_inset_at(y_bot);
    return top > bot ? top : bot;
}

lv_obj_t *safe_area_rect(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, screen_w - 2 * SAFE_INSET, screen_h - 2 * SAFE_INSET);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

lv_obj_t *safe_area_place(lv_obj_t *parent, int32_t y, int32_t height)
{
    int32_t inset = safe_area_inset_for_band(y, y + height - 1);
    int32_t w     = screen_w - 2 * inset;

    if (w <= 0) {
        return NULL;                            /*band lies entirely in the bezel*/
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, w, height);
    lv_obj_set_pos(cont, inset, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}
