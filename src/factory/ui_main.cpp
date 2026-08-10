/**
 * @file      ui.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 * @brief     Launcher, home screen, and app lifecycle owner.
 *
 * This is the root of the UI. setupGui() (at the bottom of the file) is called
 * once from factory.ino / main.cpp and builds everything else.
 *
 * Screen structure -- the whole UI lives in a two-tile LVGL tileview,
 * `main_screen`:
 *   - tile (0,0) is the launcher: a horizontally scrolling row of app icons plus
 *     the clock/status face.
 *   - tile (0,1) is the container that whichever app is currently open builds
 *     itself into.
 * Opening an app slides to tile 1 (menu_hidden()); closing it slides back
 * (menu_show()). Only one app exists at a time -- its widgets are destroyed on
 * exit.
 *
 * App registration: each `ui_<feature>.cpp` exports a global `app_t` (declared
 * extern here) and is placed on the launcher by a create_app() call in
 * setupGui(). To add an app, write the file, declare its `app_t`, and add one
 * create_app() line. Apps whose hardware is absent are skipped, so the launcher
 * reflects what the board actually has.
 *
 * Also owned here:
 *   - the LVGL input groups (`menu_g` for the launcher, `app_g` for the open
 *     app) and the encoder/keyboard focus routing between them,
 *   - the display-timeout and low-power state machine (see disp_timer_cb),
 *   - the clock face and its per-second refresh timer.
 *
 * @see LVGL tileview: https://docs.lvgl.io/master/details/widgets/tileview.html
 * @see LVGL groups:   https://docs.lvgl.io/master/details/main-components/indev.html#groups
 */
#include "ui_define.h"

// Icon bitmaps and fonts. These are generated C arrays living in
// src/factory/src/ and src/factory/src/font/ -- LV_IMG_DECLARE / LV_FONT_DECLARE
// just make the symbols visible here without a header per asset.
// @see https://docs.lvgl.io/master/details/main-components/image.html
LV_IMG_DECLARE(img_microphone);
LV_IMG_DECLARE(img_ir_remote);
LV_IMG_DECLARE(img_music);
LV_IMG_DECLARE(img_wifi);
LV_IMG_DECLARE(img_configuration);
LV_IMG_DECLARE(img_radio);
LV_IMG_DECLARE(img_gps);
LV_IMG_DECLARE(img_power);
LV_IMG_DECLARE(img_monitoring);
LV_IMG_DECLARE(img_calendar);
LV_IMG_DECLARE(img_keyboard);
LV_IMG_DECLARE(img_gyroscope);
LV_IMG_DECLARE(img_msgchat);
LV_IMG_DECLARE(img_bluetooth);
LV_IMG_DECLARE(img_test);
LV_IMG_DECLARE(img_sports);
LV_IMG_DECLARE(img_background);
LV_IMG_DECLARE(img_battery);
LV_IMG_DECLARE(img_MotionRecognition);
LV_IMG_DECLARE(img_MotorLearning);
LV_IMG_DECLARE(img_camera);
LV_IMG_DECLARE(img_si4735);
LV_IMG_DECLARE(img_track);
LV_IMG_DECLARE(img_compass);
LV_IMG_DECLARE(img_nfc);
LV_IMG_DECLARE(img_batter_low);
LV_IMG_DECLARE(img_walkie);

LV_IMG_DECLARE(img_background2);

LV_FONT_DECLARE(font_alibaba_12);
LV_FONT_DECLARE(font_alibaba_24);
LV_FONT_DECLARE(font_alibaba_40);
LV_FONT_DECLARE(font_alibaba_60);
LV_FONT_DECLARE(font_alibaba_100);

/// Reuses one of LVGL's user-definable object flags as a boolean stored on
/// `main_screen`: set when the currently open app is willing to let the device
/// sleep. Apps that must keep running (audio playback, a radio receive loop)
/// clear it via set_low_power_mode_flag(false).
#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)
/// Fallback idle timeout in ms, used when the user setting is 0/unset.
#define SCREEN_TIMEOUT 10000

lv_obj_t *main_screen;              ///< the root tileview; also declared extern in ui_define.h
lv_obj_t *menu_panel;               ///< scrolling row of app icons on tile (0,0)
lv_group_t *menu_g, *app_g;         ///< input focus groups: launcher vs. open app
static lv_timer_t *clock_timer;     ///< 1 Hz clock-face refresh
static lv_obj_t *clock_page;
static lv_timer_t *disp_timer = NULL;   ///< idle/backlight timeout state machine
static lv_timer_t *dev_timer = NULL;    ///< periodic device-status (battery etc.) refresh
static uint32_t disp_time_ms = 0;       ///< configured idle timeout in ms

/// Widgets of the clock face, cached so the 1 Hz timer can update their text
/// without walking the object tree.
typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *seg;              ///< the blinking ":" between hours and minutes
    lv_obj_t *battery_bar;
    lv_obj_t *battery_label;
} clock_label_t;;

static clock_label_t clock_label;

/// LVGL 9 replaced the global lv_msg_send() bus with per-object custom event
/// codes, which must be allocated at runtime. This holds the allocated code for
/// "the focused app's name changed"; under LVGL 8 the MSG_MENU_NAME_CHANGED
/// constant is used instead.
#if LVGL_VERSION_MAJOR == 9
static uint32_t name_change_id;
#endif


static lv_obj_t *desc_label;    ///< caption under the icon row, shows the focused app's name

// RTC_DATA_ATTR keeps these in RTC slow memory, which survives deep sleep, so
// the screen comes back at the brightness it had before sleeping rather than
// flashing to a default. On the emulator the attribute is defined away
// (ui_define.h) and they are ordinary globals.
static RTC_DATA_ATTR uint8_t brightness_level = 0;
static RTC_DATA_ATTR uint8_t keyboard_level = 0;

/**
 * Declare whether the device may drop into low power while this app is open.
 * @see disp_timer_cb(), which consumes the flag.
 */
void set_low_power_mode_flag(bool enable)
{
    if (enable) {
        lv_obj_add_flag(main_screen, DEVICE_CAN_SLEEP);
    } else {
        lv_obj_remove_flag(main_screen, DEVICE_CAN_SLEEP);
    }
}

bool get_enter_low_power_flag()
{
    bool rlst = lv_obj_has_flag(main_screen, DEVICE_CAN_SLEEP);
    return rlst;
}

/**
 * Return to the launcher: hand focus back to the launcher group, slide to tile
 * (0,0), and restart the idle timer (which is paused while an app is open, so
 * apps do not get blanked mid-use). lv_disp_trig_activity() resets the idle
 * counter so the timeout is measured from now.
 */
void menu_show()
{
    set_default_group(menu_g);
    lv_tileview_set_tile_by_index(main_screen, 0, 0, LV_ANIM_ON);
    lv_timer_resume(disp_timer);
    lv_disp_trig_activity(NULL);
    hw_feedback();
}

/// Slide to the app tile and suspend the idle timer. Paired with menu_show().
void menu_hidden()
{
    lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_ON);
    lv_timer_pause(disp_timer);
}

/// True when the launcher (rather than an app) is in front.
bool isinMenu()
{
    return !lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

/**
 * Route every input device to `group`, so navigation keys, the encoder and the
 * trackball move focus within it.
 *
 * LVGL has no "set the group on all devices" call, so this walks the linked list
 * of registered input devices with lv_indev_get_next(NULL) and assigns each one.
 * Pointer (touch) devices are included as well, which is what allows a touch to
 * take focus and keep the two input styles in sync on the watches.
 *
 * This is the mechanism behind the launcher/app split: menu_show() points
 * everything at `menu_g`, opening an app points it at `app_g`.
 */
void set_default_group(lv_group_t *group)
{
    lv_indev_t *cur_drv = NULL;
    for (;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv) {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_POINTER) {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}


/// Icon focus handler: pushes the newly focused app's name to `desc_label`.
/// The v8/v9 split exists because LVGL 9 dropped the global message bus in
/// favour of object-targeted custom events (see `name_change_id`).
static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    char *text = (char *)lv_event_get_user_data(e);
    if (c == LV_EVENT_FOCUSED) {
#if LVGL_VERSION_MAJOR == 9
        lv_obj_send_event(desc_label, (lv_event_code_t )name_change_id, text);
#else
        lv_msg_send(MSG_MENU_NAME_CHANGED, text);
#endif
    }
}

/**
 * Register one app on the launcher: create its icon button and wire the two
 * events that drive the app lifecycle.
 *
 * This is the only place an `app_t` is connected to the UI, so the plug-in
 * contract in ui_define.h is entirely realised here:
 *   - LV_EVENT_FOCUSED -> update the caption under the icon row.
 *   - LV_EVENT_CLICKED -> switch input focus to `app_g`, invoke the app's
 *     setup_func_cb with tile (0,1) as its parent, and slide that tile into view.
 *
 * `app_fun` is stored as the click callback's user data, which is why callers
 * must pass a pointer to a global (not a temporary) `app_t`.
 *
 * Note the exit path is *not* here: an app closes itself, typically via the
 * floating back button it creates, which calls its own exit_func_cb and
 * menu_show().
 *
 * @param parent   the icon row (`menu_panel`)
 * @param name     caption shown when the icon is focused; must outlive the button
 * @param img      icon bitmap, or NULL for a blank button
 * @param app_fun  the app's lifecycle callbacks
 */
static void create_app(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app_fun)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_coord_t w = 150;
    lv_coord_t h = LV_PCT(100);

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);
    // On the small 240x240 watch panels a circular icon reads better than a
    // rounded rectangle; the wider Ultra/Pager screens keep the default shape.
    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320) {
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_set_user_data(btn, (void *)name);

    if (img != NULL) {
        lv_obj_t *icon = lv_image_create(btn);
        lv_image_set_src(icon, img);
        lv_obj_center(icon);
    }
    /* Text change event callback */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_FOCUSED, (void *)name);

    /* Click to select event callback */
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        if (c == LV_EVENT_CLICKED) {
            set_default_group(app_g);
            hw_feedback();
            if (func_cb->setup_func_cb) {
                (*func_cb->setup_func_cb)(parent);
            }
            menu_hidden();
        }
    },
    LV_EVENT_CLICKED, app_fun);
}


void menu_name_label_event_cb(lv_event_t *e)
{
#if LVGL_VERSION_MAJOR == 9
    const char *v = (const char *)lv_event_get_param(e);
    if (v) {
        lv_label_set_text(lv_event_get_target_obj(e), v);
    }
#else
    lv_obj_t *label = lv_event_get_target(e);
    lv_msg_t *m = lv_event_get_msg(e);
    const char *v = (const char *)lv_msg_get_payload(m);
    if (v) {
        lv_label_set_text(label, v);
    }
#endif
}



static void clock_update_datetime(lv_timer_t *t)
{
    lv_obj_has_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) ?
    lv_obj_remove_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) :
    lv_obj_add_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN);

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(clock_label.hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(clock_label.minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(clock_label.date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
    monitor_params_t params;
    hw_get_monitor_params(params);
    lv_bar_set_value(clock_label.battery_bar, params.battery_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(clock_label.battery_label, "%d%%", params.battery_percent);
}

lv_obj_t *setupClock()
{

    const  lv_font_t *font = &font_alibaba_100;

    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_style_bg_image_src(page, &img_background2, LV_PART_MAIN);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_coord_t w = LV_PCT(35);
    lv_coord_t h = LV_PCT(70);

    int x_offset = 35;
    int y_offset = -20;

    uint32_t phy_hor_res = lv_display_get_physical_horizontal_resolution(NULL);
    if (phy_hor_res < 320) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    uint32_t phy_ver_res = lv_display_get_physical_vertical_resolution(NULL);
    if (phy_ver_res > 222) {
        h = LV_PCT(45);
    }

    if (phy_hor_res == 320 && phy_ver_res == 240) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }

    lv_obj_t *hour_cout = lv_obj_create(page);
    lv_obj_set_size(hour_cout, w, h);
    lv_obj_align(hour_cout, LV_ALIGN_LEFT_MID, x_offset, y_offset);
    lv_obj_set_style_bg_opa(hour_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hour_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(hour_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_cout = lv_obj_create(page);
    lv_obj_set_size(min_cout, w, h);
    lv_obj_align(min_cout, LV_ALIGN_RIGHT_MID, -x_offset, y_offset);
    lv_obj_set_style_bg_opa(min_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(min_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_remove_flag(min_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(page);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -10 + y_offset);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, ":");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    clock_label.seg = label;

    label = lv_label_create(hour_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "12");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    clock_label.hour = label;

    label = lv_label_create(min_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "34");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    clock_label.minute = label;

    int offset = -5;
    if (lv_display_get_physical_vertical_resolution(NULL) > 320) {
        offset = -45;
    }

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_24, LV_PART_MAIN);
    lv_label_set_text(label, "03-24 Mon");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, offset);
    clock_label.date = label;


    lv_obj_t *img = lv_image_create(page);
    lv_image_set_src(img, &img_battery);
    if (lv_display_get_physical_vertical_resolution(NULL) == 240) {
        lv_obj_align_to(img, min_cout, LV_ALIGN_OUT_BOTTOM_RIGHT, -10, 20);
    } else {
        lv_obj_align(img, LV_ALIGN_BOTTOM_RIGHT, -60, offset);
    }

    lv_obj_t *bar = lv_bar_create(img);
    lv_obj_set_size(bar, img_battery.header.w - 8, img_battery.header.h - 12);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_make(0, 255, 0), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_CENTER, -1, 0);
    clock_label.battery_bar = bar;


    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_12, LV_PART_MAIN);
    lv_label_set_text(label, "100%");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(label, img, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    clock_label.battery_label = label;

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 4);
    lv_style_set_line_color(&style_line, lv_color_white());

    lv_obj_t *line1;
    line1 = lv_line_create(page);
    static lv_point_t line_points[] = {
        {0, 0},
        {150, 0}
    };
    lv_line_set_points(line1, line_points, 2);
    lv_obj_add_style(line1, &style_line, 0);
    lv_obj_align(line1, LV_ALIGN_BOTTOM_MID, 0, 15);
    lv_obj_set_style_line_opa(line1, LV_OPA_60, LV_PART_MAIN);


    clock_timer = lv_timer_create(clock_update_datetime, 1000, NULL);
    lv_timer_pause(clock_timer);

    return page;
}

#if  defined(USING_TOUCHPAD) || defined(USING_TOUCH_KEYBOARD)
typedef struct {
    lv_obj_t *obj;
    int id;
} ChildObject;

static void scrollbar_change_cb(lv_event_t *e)
{
    lv_obj_t *tileview = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *panel = lv_event_get_target_obj(e);
    lv_coord_t view_x = lv_obj_get_scroll_x(panel);
    lv_coord_t view_width = lv_obj_get_width(panel);
    int child_count = lv_obj_get_child_count(panel);
    ChildObject *child_objects = (ChildObject *)lv_malloc(child_count * sizeof(ChildObject));

    for (int i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(panel, i);
        child_objects[i].obj = child;
        child_objects[i].id = i;
    }

    uint8_t view_obj_count = 0;
    int32_t last_id = -1;
    for (int i = 0; i < child_count; i++) {
        lv_obj_t *child = child_objects[i].obj;
        lv_coord_t child_x = lv_obj_get_x(child);
        lv_coord_t child_width = lv_obj_get_width(child);

        if (child_x + child_width > view_x && child_x < view_x + view_width) {
            last_id = child_objects[i].id;
            view_obj_count++;
        }
    }

    if (last_id != -1) {
        lv_obj_t *obj = child_objects[last_id].obj;
        if (lv_obj_get_child(panel, 1) == obj || view_obj_count == 3) {
            last_id -= 1;
        }
        const char *name = (const char *)lv_obj_get_user_data(child_objects[last_id].obj);
        if (name) {
#if LVGL_VERSION_MAJOR == 9
            lv_obj_send_event(desc_label, (lv_event_code_t )name_change_id, (void *)name);
#else
            lv_msg_send(MSG_MENU_NAME_CHANGED, (void *)name);
#endif
        }
    }

    lv_free(child_objects);

}
#endif


/**
 * Low-battery watchdog, run periodically by `dev_timer`.
 *
 * Below 3300 mV a single-cell lithium battery is nearly exhausted, and letting
 * it drop further risks damaging the cell -- so rather than warn, the firmware
 * shuts itself down. The `usb_voltage == 0` term is essential: while charging,
 * the measured battery voltage can legitimately sit low, and shutting down on
 * that would make the device impossible to recharge.
 *
 * lv_refr_now() forces the warning screen to be drawn synchronously, since
 * hw_shutdown() never returns and the normal redraw would never happen.
 */
static void hw_device_poll(lv_timer_t *t)
{
    monitor_params_t params;
    hw_get_monitor_params(params);
    if (params.battery_voltage < 3300 && params.usb_voltage == 0) {
        printf("Low battery voltage: %lu mV USB Voltage: %lu mV\n", params.battery_voltage, params.usb_voltage);
        lv_obj_clean(lv_screen_active());
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_radius(lv_screen_active(), 0, 0);

        lv_obj_t *image = lv_image_create(lv_screen_active());
        lv_image_set_src(image, &img_batter_low);
        lv_obj_center(image);

        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Battery Low!\nShutting down...");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

        lv_refr_now(NULL);
        lv_delay_ms(3000);
        hw_shutdown();
    }
}

/**
 * Idle / power-state machine, run periodically by `disp_timer`.
 *
 * There are three states, entered in sequence as the device stays idle, and any
 * user input collapses straight back to the first:
 *
 *  1. ACTIVE     -- launcher visible, CPU at 240 MHz, keyboard backlight on.
 *
 *  2. WATCH FACE -- after SCREEN_TIMEOUT of inactivity, *and* only if the open
 *     app permits it (get_enter_low_power_flag(), set via
 *     set_low_power_mode_flag()). The launcher is hidden, the clock page shown,
 *     the keyboard backlight saved and switched off, and the CPU dropped to
 *     80 MHz. The keyboard level is stashed in `keyboard_level` so it can be
 *     restored exactly, rather than reset to a default.
 *
 *  3. DISPLAY OFF -- after a further hw_get_disp_timeout_ms() (the user setting;
 *     0 disables this step entirely). The backlight is faded out, its level
 *     saved in RTC memory, and hw_low_power_loop() puts the SoC into light
 *     sleep. That call blocks until a wake source fires, so everything after it
 *     is the wake-up path: restore the screen, clock speed and brightness, and
 *     reset the activity timer so the machine restarts from state 1.
 *
 * NO_ENTER_LIGHT_SLEEP swaps the light-sleep call for a busy-wait on the BOOT
 * button, which is useful when debugging, since light sleep disconnects USB
 * serial.
 */
static void ui_poll_timer_callback(lv_timer_t *t)
{
    bool timeout = lv_display_get_inactive_time(NULL) > SCREEN_TIMEOUT;
    if (timeout) {
        if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN) && get_enter_low_power_flag()) {
            lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);

            keyboard_level = hw_get_kb_backlight();
            hw_set_kb_backlight(0);
            lv_obj_remove_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_timer_resume(clock_timer);

            hw_set_cpu_freq(80);

            if (hw_get_disp_timeout_ms() != 0) {
                disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
            } else {
                disp_time_ms = 0;
            }
        }
    } else {
        if (!lv_obj_has_flag(clock_page, LV_OBJ_FLAG_HIDDEN)) {

            hw_set_cpu_freq(240);

            lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(clock_timer);

            hw_set_kb_backlight(keyboard_level);
        }
    }

    if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
        bool disp_on = hw_get_disp_is_on();
        if (disp_on && disp_time_ms != 0) {
            if (lv_tick_get() > disp_time_ms) {
                printf("Disp off\n");

                brightness_level =  hw_get_disp_backlight();
                printf("brightness_level:%d\n", brightness_level);

                hw_dec_brightness(0);

                hw_low_power_loop();
#ifdef NO_ENTER_LIGHT_SLEEP
                printf("Enter sleep\n");
                pinMode(0, INPUT_PULLUP);
                while (digitalRead(0) == HIGH) {
                    delay(10);
                }
                printf("Wakeup\n");
#endif
                lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(clock_timer);

                hw_set_cpu_freq(240);

                lv_refr_now(NULL);

                lv_display_trigger_activity(NULL);

                hw_inc_brightness(brightness_level);

                hw_set_kb_backlight(keyboard_level);
            }
        }
    }
}

/**
 * Build the entire UI. Called once, from setup() on hardware or main() on the
 * emulator, after hw_init() has brought the peripherals up.
 *
 * Order of construction:
 *   1. Splash screen (blocking, 5 s).
 *   2. Theme: LVGL's dark default theme, restyled by theme_init() in ui_theme.cpp.
 *      MAIN_FONT comes from the per-board block in hal_interface.h, so the text
 *      size matches the panel.
 *   3. Input groups and the two-tile tileview described at the top of this file.
 *   4. App registration -- the long run of create_app() calls below.
 *   5. Clock face, then the periodic timers.
 */
void setupGui()
{

    // Splash screen. lv_refr_now() draws it synchronously, then the UI is
    // blocked for 5 seconds -- nothing else can run during this delay, which is
    // acceptable only because it happens once at boot.
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);
    lv_obj_t *start_logo = lv_label_create(lv_screen_active());
    lv_label_set_text(start_logo, "LilyGo");
    LV_FONT_DECLARE(font_logo_84);
    lv_obj_set_style_text_font(start_logo, &font_logo_84, LV_PART_MAIN);
    lv_obj_set_style_text_color(start_logo, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(start_logo);
    lv_refr_now(NULL);
    lv_delay_ms(5000);
    lv_obj_delete(start_logo);

    disable_keyboard();

    const lv_font_t  *main_font = MAIN_FONT;
    lv_theme_default_init(NULL, lv_color_black(), lv_palette_darken(LV_PALETTE_GREY, 3),
                          LV_THEME_DEFAULT_DARK, main_font);

    theme_init();

    // Two focus groups, swapped by set_default_group() as apps open and close.
    // The launcher starts focused.
    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    static lv_style_t style_frameless;
    lv_style_init(&style_frameless);
    lv_style_set_radius(&style_frameless, 0);
    lv_style_set_border_width(&style_frameless, 0);
    lv_style_set_bg_color(&style_frameless, lv_color_white());
    lv_style_set_shadow_width(&style_frameless, 55);
    lv_style_set_shadow_color(&style_frameless, lv_color_black());

    /* opening animation */
    main_screen = lv_tileview_create(lv_screen_active());

    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    /* Create two views for switching menus and app UI */
    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Initialize the menu view */
    // The icon row: a flex row that scrolls horizontally and snaps each icon to
    // the centre, so a swipe or encoder turn always lands on exactly one app.
    lv_obj_t *panel = lv_obj_create(menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(70));
    lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(panel, &style_frameless, 0);
#if  defined(USING_TOUCHPAD) || defined(USING_TOUCH_KEYBOARD)
    lv_obj_add_event_cb(panel, scrollbar_change_cb, LV_EVENT_SCROLL_END, NULL);
#endif

    // Each app's `app_t` is defined in its own ui_*.cpp and declared here rather
    // than in a shared header, which keeps the app list in one readable place.
    extern app_t ui_sys_main ;
    extern app_t ui_radio_main ;
    extern app_t ui_audio_main ;
    extern app_t ui_wireless_main ;
    extern app_t ui_gps_main ;
    extern app_t ui_monitor_main ;
    extern app_t ui_power_main ;
    extern app_t ui_calendar_main;
    extern app_t ui_info_main;
    extern app_t ui_microphone_main;
    extern app_t ui_keyboard_main;
    extern app_t ui_sensor_main;
    extern app_t ui_msgchat_main;
    extern app_t ui_ble_main;
    extern app_t ui_ble_kb_main;
    extern app_t ui_factory_main;

    /* Add application */
    // The launcher contents are built from the board's capabilities, so the same
    // source produces a different app list per device. Three kinds of gate are
    // used below:
    //   - #if defined(USING_*)  -- the part is fitted on this board at all
    //     (resolved by the per-board block in hal_interface.h);
    //   - a runtime check such as hw_has_keyboard() -- the part is fitted but may
    //     not have answered when probed;
    //   - a toolchain version check, for apps that only build against certain
    //     arduino-esp32 releases (see the Walkie app below).
    // Unconditional create_app() calls are apps every board supports.
    //
    // Icon order here is the order they appear on the launcher.
#if defined(USING_IR_REMOTE)
    extern app_t ui_ir_remote_main;
    create_app(panel, "IR Remote", &img_ir_remote, &ui_ir_remote_main);
#endif

#if defined(USING_EXTERN_NRF2401)
    extern app_t ui_nrf24_main;
    create_app(panel, "NRF24", &img_radio, &ui_nrf24_main);
#endif

#if defined(USING_BLE_CONTROL)
    create_app(panel, "Camera Remote", &img_camera, &ui_camera_remote_main);
#endif

#if defined(USING_SI473X_RADIO)
    extern app_t ui_si4735_main;
    create_app(panel, "Radio", &img_si4735, &ui_si4735_main);
#endif

#if defined(USING_MAG_QMC5883)
    extern app_t ui_compass_main;
    create_app(panel, "Compass", &img_compass, &ui_compass_main);
#endif

#if defined(USING_TRACKBALL)
    extern app_t ui_trackball_main;
    create_app(panel, "Trackball", &img_track, &ui_trackball_main);
#endif

#if defined(USING_ST25R3916)
    extern app_t ui_nfc_main;
    create_app(panel, "NFC", &img_nfc, &ui_nfc_main);
#endif

    // #if defined(TODO://)
    // extern app_t ui_recorder_main;
    // create_app(panel, "Recorder", &img_track, &ui_recorder_main);
    // #endif

    create_app(panel, "Screen Test", &img_test, &ui_factory_main);
    create_app(panel, "Setting", &img_configuration, &ui_sys_main);
    create_app(panel, "Wireless", &img_wifi, &ui_wireless_main);

#if defined(USING_UART_BLE)
    create_app(panel, "Bluetooth", &img_bluetooth, &ui_ble_main);
#endif

#if defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_has_keyboard()) {
        create_app(panel, "BLE Keyboard", &img_bluetooth, &ui_ble_kb_main);
        create_app(panel, "Keyboard", &img_keyboard, &ui_keyboard_main);
    }
#endif

    create_app(panel, "Music", &img_music, &ui_audio_main);
    create_app(panel, "LoRa", &img_radio, &ui_radio_main);
    create_app(panel, "LoRa Chat", &img_msgchat, &ui_msgchat_main);

    // The walkie-talkie app needs both the Pager's codec hardware and an
    // arduino-esp32 core of 3.0.0 or older -- its I2S usage does not compile
    // against newer cores. It therefore disappears from the launcher if the core
    // is upgraded, rather than breaking the build.
#if (ESP_ARDUINO_VERSION <= ESP_ARDUINO_VERSION_VAL(3,0,0)) && defined(ARDUINO_T_LORA_PAGER)
    extern app_t ui_walkie_main;
    create_app(panel, "Walkie", &img_walkie, &ui_walkie_main);
#endif

    create_app(panel, "GPS", &img_gps, &ui_gps_main);
    create_app(panel, "Monitor", &img_monitoring, &ui_monitor_main);
    create_app(panel, "Power", &img_power, &ui_power_main);
    create_app(panel, "Microphone", &img_microphone, &ui_microphone_main);
    create_app(panel, "IMU", &img_gyroscope, &ui_sensor_main);

    int offset = -10;
    if (lv_display_get_physical_vertical_resolution(NULL) > 320) {
        offset = -45;
    }
    /* Initialize the label */
    desc_label = lv_label_create(menu_panel);
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, offset);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);

    if (lv_display_get_physical_horizontal_resolution(NULL) < 320) {
        lv_obj_set_style_text_font(desc_label, &font_alibaba_24, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -25);
    } else {
        lv_obj_set_style_text_font(desc_label, &font_alibaba_40, 0);
    }
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    name_change_id =  lv_event_register_id();
    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, (lv_event_code_t )name_change_id, NULL);
    lv_obj_send_event(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);
#else
    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, LV_EVENT_MSG_RECEIVED, NULL);
    lv_msg_subsribe_obj(MSG_MENU_NAME_CHANGED, desc_label, NULL);
    lv_event_send(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);
#endif

    lv_obj_update_snap(panel, LV_ANIM_ON);


    clock_page = setupClock();
    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);

    disp_timer = lv_timer_create(ui_poll_timer_callback, 1000, NULL);

    dev_timer = lv_timer_create(hw_device_poll, 5000, NULL);

    // Allow low power mode
    set_low_power_mode_flag(true);
    lv_display_trigger_activity(NULL);
}




static lv_obj_t *canvas;
static lv_indev_t *touch_indev;

void touch_panel_init()
{
    uint32_t width = lv_disp_get_hor_res(NULL);
    uint32_t height = lv_disp_get_ver_res(NULL);
#if 1
    lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
    uint32_t buffer_size =    LV_DRAW_BUF_SIZE(width, height, cf);
    uint8_t *buf_draw_buf = (uint8_t *)malloc(buffer_size);
    uint16_t stride_size = LV_DRAW_BUF_STRIDE(width, cf);

    printf("data_size:%u\n", buffer_size);
    printf("stride:%u\n", stride_size);
    printf("cf:%u\n", cf);

    static lv_draw_buf_t draw_buf = {
        .header = {
            .magic = (0x19),
            .cf = (cf),
            .flags = LV_IMAGE_FLAGS_MODIFIABLE,
            .w = (width),
            .h = (height),
            .stride = stride_size,
            .reserved_2 = 0,
        },
        .data_size = buffer_size,
        .data = buf_draw_buf,
        .unaligned_data = buf_draw_buf,
    };

    lv_image_header_t *header = &draw_buf.header;
    lv_draw_buf_init(&draw_buf, header->w, header->h,
                     (lv_color_format_t)header->cf,
                     header->stride,
                     buf_draw_buf,
                     buffer_size);
    lv_draw_buf_set_flag(&draw_buf, LV_IMAGE_FLAGS_MODIFIABLE);

    printf("data_size:%u\n", draw_buf.data_size);
    printf("stride:%u\n", draw_buf.header.stride);
    printf("cf:%u\n", draw_buf.header.cf);

#else
    // /*Create a buffer for the canvas*/
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
#endif

    /*Create a canvas and initialize its palette*/
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
    lv_obj_center(canvas);


    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            touch_indev = indev;
            break;
        }
        indev = lv_indev_get_next(indev);
    }
    lv_indev_type_t type = lv_indev_get_type(touch_indev);
    if (type != LV_INDEV_TYPE_POINTER) {
        return;
    }

    lv_timer_create([](lv_timer_t *t) {

#undef lv_point_t
        lv_point_t  point;
        lv_indev_state_t state =  lv_indev_get_state(touch_indev);
        if ( state == LV_INDEV_STATE_PRESSED ) {
            lv_indev_get_point(touch_indev, &point);
            printf("%d %d\n", point.x, point.y);

            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);

            lv_draw_arc_dsc_t dsc;
            lv_draw_arc_dsc_init(&dsc);
            dsc.color = lv_palette_main(LV_PALETTE_RED);
            dsc.width = 2;
            dsc.center.x =  point.x;
            dsc.center.y = point.y;
            dsc.width = 10;
            dsc.radius = 6;
            dsc.start_angle = 0;
            dsc.end_angle = 360;
            lv_draw_arc(&layer, &dsc);
            lv_canvas_finish_layer(canvas, &layer);
        }
    }, 30, NULL);

}


























