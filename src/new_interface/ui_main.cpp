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
 *   - tile (0,0) is `menu_panel`, the home screen built by ui_home_build():
 *     a clock (digital or analog, tap to toggle), battery status, and a row
 *     of pinned-link icons capped at PINNED_APPS_MAX_VISIBLE plus a trailing
 *     "All Apps" icon (ui_all_apps.cpp) that lists every registered app.
 *   - tile (0,1) is the container that whichever app is currently open builds
 *     itself into.
 * Opening an app slides to tile 1 (menu_hidden()); closing it slides back
 * (menu_show()). Only one app exists at a time -- its widgets are destroyed on
 * exit.
 *
 * App registration: each `ui_<feature>.cpp` exports a global `app_t` (declared
 * extern here) and is added to the app_registry() table by a register_app()
 * call in setupGui(), gated exactly as before (board capability macros,
 * runtime hw_has_*() probes). ui_pinned_links.cpp and ui_all_apps.cpp read
 * that table rather than building their own; opening any app -- from the
 * pinned row, "All Apps", or in principle any future launcher UI -- goes
 * through the single open_app() path declared in ui_define.h. To add an app,
 * write the file, declare its `app_t`, and add one register_app() line. Apps
 * whose hardware is absent are skipped, so the launcher reflects what the
 * board actually has.
 *
 * Also owned here:
 *   - the LVGL input groups (`menu_g` for the launcher, `app_g` for the open
 *     app) and the encoder/keyboard focus routing between them,
 *   - the display-timeout and low-power state machine (see
 *     ui_poll_timer_callback()).
 *
 * @see LVGL tileview: https://docs.lvgl.io/master/details/widgets/tileview.html
 * @see LVGL groups:   https://docs.lvgl.io/master/details/main-components/indev.html#groups
 */
#include "ui_define.h"
#include "app_config.h"
#include "ui_home.h"
#include "ui_notification_popup.h"

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

/// Reuses one of LVGL's user-definable object flags as a boolean stored on
/// `main_screen`: set when the currently open app is willing to let the device
/// sleep. Apps that must keep running (audio playback, a radio receive loop)
/// clear it via set_low_power_mode_flag(false).
#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)
/// Fallback idle timeout in ms, used when the user setting is 0/unset.
#define SCREEN_TIMEOUT 10000

lv_obj_t *main_screen;              ///< the root tileview; also declared extern in ui_define.h
lv_obj_t *menu_panel;               ///< the home screen (clock/battery/pinned links) on tile (0,0)
lv_group_t *menu_g, *app_g;         ///< input focus groups: launcher vs. open app
static lv_timer_t *disp_timer = NULL;   ///< idle/backlight timeout state machine
static lv_timer_t *dev_timer = NULL;    ///< periodic device-status (battery etc.) refresh
static uint32_t disp_time_ms = 0;       ///< configured idle timeout in ms
/// True from the moment the idle timeout drops the CPU clock/keyboard
/// backlight (formerly the separate "WATCH FACE" state) until the next
/// activity restores them. See ui_poll_timer_callback().
static bool low_power_active = false;

/// One entry per app that survived its board/runtime gate in setupGui(),
/// built by register_app() and handed out through app_registry() so
/// ui_pinned_links.cpp / ui_all_apps.cpp can list/launch apps without
/// re-deriving which ones exist on this board.
#define MAX_APP_REGISTRY 32
static AppEntry s_app_registry[MAX_APP_REGISTRY];
static size_t s_app_registry_count = 0;

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


/**
 * Open `app` on tile (0,1): the one launch path declared in ui_define.h.
 *
 * Every icon that can start an app -- the original launcher row, the home
 * screen's pinned-links row, "All Apps" -- calls this instead of duplicating
 * the click behaviour, which used to live inline in create_app()'s click
 * lambda.
 *
 * Note the exit path is *not* here: an app closes itself, typically via the
 * floating back button it creates, which calls its own exit_func_cb and
 * menu_show().
 */
void open_app(app_t *app)
{
    if (!app) {
        return;
    }
    set_default_group(app_g);
    hw_feedback();
    if (app->setup_func_cb) {
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        (*app->setup_func_cb)(parent);
    }
    menu_hidden();
}

/**
 * Build one launcher-style icon button in `parent`, wired to open_app(app)
 * on click.
 *
 * @param parent   the icon row to add the button to
 * @param name     caption; stored as the button's user data (unused by the
 *                 button itself now that the focus caption is gone, but kept
 *                 for parity with the original widget and for callers that
 *                 want to read it back)
 * @param img      icon bitmap, or NULL for a blank button
 * @param app      the app's lifecycle callbacks
 */
lv_obj_t *create_app_icon(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app)
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

    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        if (c == LV_EVENT_CLICKED) {
            open_app(func_cb);
        }
    },
    LV_EVENT_CLICKED, app);

    return btn;
}

/**
 * Append one entry to the app registry, gated exactly as its caller in
 * setupGui() is (an `#if defined(USING_*)` block, a runtime hw_has_*()
 * check, or none at all for board-universal apps). Does not touch the UI --
 * setupGui() iterates the finished table once to build the icons.
 */
static void register_app(const char *name, const lv_img_dsc_t *icon, app_t *app, int pin_id)
{
    if (s_app_registry_count >= MAX_APP_REGISTRY) {
        printf("app registry full, dropping \"%s\"\n", name);
        return;
    }
    AppEntry entry = { name, icon, app, pin_id };
    s_app_registry[s_app_registry_count++] = entry;
}

const AppEntry *app_registry(size_t *count)
{
    if (count) {
        *count = s_app_registry_count;
    }
    return s_app_registry;
}


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

    hw_lora_battery_saver_poll();

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
 * Tile (0,0) is the home screen -- clock, battery, pinned links -- built once
 * by ui_home_build() and never hidden, so unlike the original three-state
 * machine there is no separate watch-face page to show or hide here. What's
 * left are two states, entered in sequence as the device stays idle, and any
 * user input collapses straight back to the first:
 *
 *  1. ACTIVE      -- CPU at 240 MHz, keyboard backlight on.
 *
 *  2. POWER SAVE  -- after SCREEN_TIMEOUT of inactivity, *and* only if the
 *     open app permits it (get_enter_low_power_flag(), set via
 *     set_low_power_mode_flag()). The keyboard backlight is saved and
 *     switched off and the CPU dropped to 80 MHz while the clock keeps
 *     ticking on tile (0,0). The keyboard level is stashed in
 *     `keyboard_level` so it can be restored exactly, rather than reset to a
 *     default. `low_power_active` tracks this state (this timer only runs
 *     while tile (0,0) is in front -- see menu_show()/menu_hidden() -- so an
 *     open app is never affected by it).
 *
 *     After a further hw_get_disp_timeout_ms() of state 2 (the user setting;
 *     0 disables this step entirely) the backlight is faded out, its level
 *     saved in RTC memory, and hw_low_power_loop() puts the SoC into light
 *     sleep. That call blocks until a wake source fires, so everything after
 *     it is the wake-up path: restore the clock speed and brightness, and
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
        if (!low_power_active && get_enter_low_power_flag()) {
            low_power_active = true;

            keyboard_level = hw_get_kb_backlight();
            hw_set_kb_backlight(0);

            hw_set_cpu_freq(80);

            if (hw_get_disp_timeout_ms() != 0) {
                disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
            } else {
                disp_time_ms = 0;
            }
        }
    } else {
        if (low_power_active) {
            low_power_active = false;

            hw_set_cpu_freq(240);
            hw_set_kb_backlight(keyboard_level);
        }
    }

    if (low_power_active) {
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
                low_power_active = false;

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
 *   4. App registration -- the run of register_app() calls below, gated per
 *      board/runtime capability, building the app_registry() table.
 *   5. The home screen (ui_home_build()), then the periodic timers and the
 *      notification-popup listener.
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

    /* opening animation */
    main_screen = lv_tileview_create(lv_screen_active());

    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    /* Create two views for switching menus and app UI */
    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Each app's `app_t` is defined in its own ui_*.cpp and declared here rather
    // than in a shared header, which keeps the app list in one readable place.
    extern app_t ui_sys_main ;
    extern void ui_sys_init(void);
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

    /* Register applications */
    // The launcher contents are built from the board's capabilities, so the same
    // source produces a different app list per device. Three kinds of gate are
    // used below:
    //   - #if defined(USING_*)  -- the part is fitted on this board at all
    //     (resolved by the per-board block in hal_interface.h);
    //   - a runtime check such as hw_has_keyboard() -- the part is fitted but may
    //     not have answered when probed;
    //   - a toolchain version check, for apps that only build against certain
    //     arduino-esp32 releases (see the Walkie app below).
    // Unconditional register_app() calls are apps every board supports.
    //
    // Registration order is icon order, both in the original scrolling row and
    // in ui_all_apps.cpp's listing. The 4th argument is the PinnableApp id
    // (app_config.h) this app can be pinned to the home screen as, or -1 if it
    // cannot be pinned -- keep these in sync with the PinnableApp enum.
#if defined(USING_IR_REMOTE)
    extern app_t ui_ir_remote_main;
    register_app("IR Remote", &img_ir_remote, &ui_ir_remote_main, PIN_IR_REMOTE);
#endif

#if defined(USING_EXTERN_NRF2401)
    extern app_t ui_nrf24_main;
    register_app("NRF24", &img_radio, &ui_nrf24_main, -1);
#endif

#if defined(USING_BLE_CONTROL)
    register_app("Camera Remote", &img_camera, &ui_camera_remote_main, -1);
#endif

#if defined(USING_SI473X_RADIO)
    extern app_t ui_si4735_main;
    register_app("Radio", &img_si4735, &ui_si4735_main, -1);
#endif

#if defined(USING_MAG_QMC5883)
    extern app_t ui_compass_main;
    register_app("Compass", &img_compass, &ui_compass_main, -1);
#endif

#if defined(USING_TRACKBALL)
    extern app_t ui_trackball_main;
    register_app("Trackball", &img_track, &ui_trackball_main, -1);
#endif

#if defined(USING_ST25R3916)
    extern app_t ui_nfc_main;
    register_app("NFC", &img_nfc, &ui_nfc_main, -1);
#endif

    // #if defined(TODO://)
    // extern app_t ui_recorder_main;
    // register_app("Recorder", &img_track, &ui_recorder_main, -1);
    // #endif

    register_app("Screen Test", &img_test, &ui_factory_main, -1);
    register_app("Setting", &img_configuration, &ui_sys_main, PIN_SETTINGS);
    register_app("Wireless", &img_wifi, &ui_wireless_main, PIN_WIRELESS);

#if defined(USING_UART_BLE)
    register_app("Bluetooth", &img_bluetooth, &ui_ble_main, -1);
#endif

#if defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_has_keyboard()) {
        register_app("BLE Keyboard", &img_bluetooth, &ui_ble_kb_main, -1);
        register_app("Keyboard", &img_keyboard, &ui_keyboard_main, -1);
    }
#endif

    register_app("Music", &img_music, &ui_audio_main, -1);
    register_app("LoRa", &img_radio, &ui_radio_main, PIN_LORA);
    register_app("LoRa Chat", &img_msgchat, &ui_msgchat_main, -1);

    // The walkie-talkie app needs both the Pager's codec hardware and an
    // arduino-esp32 core of 3.0.0 or older -- its I2S usage does not compile
    // against newer cores. It therefore disappears from the launcher if the core
    // is upgraded, rather than breaking the build.
#if (ESP_ARDUINO_VERSION <= ESP_ARDUINO_VERSION_VAL(3,0,0)) && defined(ARDUINO_T_LORA_PAGER)
    extern app_t ui_walkie_main;
    register_app("Walkie", &img_walkie, &ui_walkie_main, -1);
#endif

    register_app("GPS", &img_gps, &ui_gps_main, PIN_GPS);
    register_app("Monitor", &img_monitoring, &ui_monitor_main, PIN_MONITOR);
    register_app("Power", &img_power, &ui_power_main, -1);
    register_app("Microphone", &img_microphone, &ui_microphone_main, -1);
    register_app("IMU", &img_gyroscope, &ui_sensor_main, -1);

    // Home screen: clock (digital/analog, tap to toggle), battery status, and
    // the pinned-links row (which reads the table just built through
    // app_registry()), all built inside tile (0,0).
    ui_home_build(menu_panel);

    disp_timer = lv_timer_create(ui_poll_timer_callback, 1000, NULL);

    dev_timer = lv_timer_create(hw_device_poll, 5000, NULL);

    ui_notification_popup_init();
    ui_sys_init();

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


























