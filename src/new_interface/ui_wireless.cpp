/**
 * @file      ui_wireless.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
/**
 * @brief WiFi app -- scan for networks and join one.
 *
 * Scans, presents the results in a dropdown, takes a password, and hands the
 * credentials to hw_set_wifi_connect(). The connection attempt itself is shown
 * by the modal progress bar in ui_msg.cpp, which reports success (with the
 * assigned IP) or the specific reason for failure.
 *
 * Joining a network is what enables NTP time sync -- see the WiFiGotIP handler
 * in factory.ino, which starts SNTP once an IP is assigned and writes the
 * corrected time into the hardware RTC.
 *
 * Text entry follows the same pattern as the chat app: an on-screen LVGL
 * keyboard on touch boards, the physical keyboard on the T-LoRa-Pager.
 *
 * Credentials are used for the current session only; nothing is stored here
 * (factory.ino explicitly disables WiFi's own credential persistence).
 */
#include "ui_define.h"


#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;
#endif

static lv_obj_t *menu = NULL;
static char wifi_ssid[64];          ///< SSID chosen from the scan results
static char wifi_password[128];     ///< password as typed
static lv_obj_t *wifi_dd;           ///< dropdown listing the scan results
static bool scanning = false;       ///< a scan is in flight; blocks starting another

extern void ui_show_wifi_process_bar();

// --- Persistent connection status section -----------------------------------
// A small read-only readout (state/SSID/IP/RSSI) kept refreshed while this
// screen is open, following the same "poll hw_get_* on a timer" idiom as
// sys_timer_event_cb() in ui_sys.cpp.
static lv_obj_t *status_state_label = NULL;
static lv_obj_t *status_ssid_label = NULL;
static lv_obj_t *status_ip_label = NULL;
static lv_obj_t *status_rssi_label = NULL;
static lv_timer_t *status_timer = NULL;

static void status_timer_event_cb(lv_timer_t *t)
{
    bool connected = hw_get_wifi_connected();
    lv_label_set_text(status_state_label, connected ? "Connected" : "Disconnected");

    string ssid = "N.A";
    hw_get_wifi_ssid(ssid);
    lv_label_set_text(status_ssid_label, ssid.empty() ? "N.A" : ssid.c_str());

    string ip = "N.A";
    hw_get_ip_address(ip);
    lv_label_set_text(status_ip_label, ip.empty() ? "N.A" : ip.c_str());

    if (connected) {
        lv_label_set_text_fmt(status_rssi_label, "%d dBm", hw_get_wifi_rssi());
    } else {
        lv_label_set_text(status_rssi_label, "N.A");
    }
}

/// Stop and free the status timer. Called both from back_event_handler() --
/// the only route out of this screen, see the other apps' back handlers -- and
/// from ui_wireless_exit(), so the timer cannot outlive this screen either way.
static void status_timer_stop()
{
    if (status_timer) {
        lv_timer_del(status_timer);
        status_timer = NULL;
    }
}

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
        status_timer_stop();
#ifdef USING_TOUCHPAD
        if (keyboard) {
            lv_obj_del(keyboard);
            keyboard = NULL;
        }
#endif
        lv_obj_clean(menu);
        lv_obj_del(menu);
        disable_keyboard();
        menu_show();
    }
}

static void password_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);

    bool copyToBuffer = false;

    if (code != LV_EVENT_DRAW_MAIN_BEGIN &&
            code != LV_EVENT_DRAW_MAIN &&
            code != LV_EVENT_DRAW_MAIN_END &&
            code != LV_EVENT_DRAW_POST_BEGIN &&
            code != LV_EVENT_DRAW_POST && code != LV_EVENT_DRAW_POST_END
       )
        printf("ta event code:%d state:%d edited:%d\n", code, state, edited);

#ifdef USING_TOUCHPAD
    if (code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        copyToBuffer = true;
    }
#endif

    if (code == LV_EVENT_CLICKED) {

#if defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)
        enable_keyboard();
#else
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            printf("disable keyboard\n");
            disable_keyboard();
            copyToBuffer = true;
        }
#ifdef USING_TOUCHPAD
        else {
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
#endif
#endif  /*defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)*/
    }
    
#if defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)
    else if (code == LV_EVENT_DEFOCUSED) {

        printf("disable keyboard\n");
        disable_keyboard();

    }
#endif  /*defined(USING_INPUT_DEV_KEYBOARD) && !defined(USING_INPUT_DEV_ROTARY)*/

    else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            printf("enable input keyboard \n");
            enable_keyboard();
        }
    }

#ifdef USING_TOUCHPAD
    else if (code == LV_EVENT_DEFOCUSED) {
        if (!state) {
            lv_keyboard_set_textarea(keyboard, NULL);
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            copyToBuffer = true;
        }
    }
#endif

    if (copyToBuffer) {
        const char  *password =  lv_textarea_get_text(ta);
        if (!password) {
            printf("PWD IS NULL!\n");
            return;
        }
        if (lv_strlen(password) > 0) {
            lv_strncpy(wifi_password, password, lv_strlen(password));
            printf("PWD:%s\n", wifi_password);
        } else {
            printf("PWD IS EMPTY!\n");
        }
    }
}

static lv_obj_t *password_text_crate(lv_obj_t *parent)
{
    lv_obj_t *pwd_ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(pwd_ta, "password");
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_obj_set_scrollbar_mode(pwd_ta, LV_SCROLLBAR_MODE_OFF);

#ifdef WIFI_PASSWORD
    lv_textarea_set_text(pwd_ta, WIFI_PASSWORD);
    lv_strncpy(wifi_password, WIFI_PASSWORD, lv_strlen(WIFI_PASSWORD));
#endif

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_add_event_cb(pwd_ta, password_ta_event_cb, LV_EVENT_ALL, NULL);

    return pwd_ta;
}

static void wifi_connect_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    wifi_conn_params_t params;

    printf("-> Connect to SSID : %s\n", wifi_ssid);
    printf("-> Connect to PWD  : %s\n", wifi_password);
    if (lv_strlen(wifi_ssid) == 0 ) {
        ui_msg_pop_up("WiFi", "SSID is null");
    } else if (lv_strlen(wifi_password) == 0 ) {
        ui_msg_pop_up("WiFi", "Password is null");
    } else if ( lv_strlen(wifi_password) < 8) {
        ui_msg_pop_up("WiFi", "Password too short");
    } else {
        params.ssid = wifi_ssid;
        params.password = wifi_password;
        hw_set_wifi_connect(params);
        ui_show_wifi_process_bar();
    }
}

static void dropdown_event(lv_event_t *e)
{
    char buf[128];
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_dropdown_get_selected_str(dd, buf, 128);
        printf("select option = %s\n", buf);
        lv_strcpy(wifi_ssid, buf);
#ifdef WIFI_SSID
        if (lv_strcmp(wifi_ssid, WIFI_SSID) == 0) {
            lv_strncpy(wifi_password, WIFI_PASSWORD, lv_strlen(WIFI_PASSWORD));
            printf("Copy ssid:%s password to buffer:%s\n", wifi_ssid, wifi_password);
        }
#endif
#ifdef WIFI_SSID2
        if (lv_strcmp(wifi_ssid, WIFI_SSID2) == 0) {
            lv_strncpy(wifi_password, WIFI_PASSWORD2, lv_strlen(WIFI_PASSWORD2));
            printf("Copy ssid:%s password to buffer:%s\n", wifi_ssid, wifi_password);
        }
#endif
    }
}


static lv_obj_t *dropdown_create(lv_obj_t *parent)
{
    wifi_dd = lv_dropdown_create(parent);
    lv_dropdown_clear_options(wifi_dd);
    lv_obj_add_event_cb(wifi_dd, dropdown_event, LV_EVENT_VALUE_CHANGED, NULL);
#ifdef WIFI_SSID
    lv_dropdown_add_option(wifi_dd, WIFI_SSID, 0);
    lv_strcpy(wifi_ssid, WIFI_SSID);
#endif
#ifdef WIFI_SSID2
    lv_dropdown_add_option(wifi_dd, WIFI_SSID2, 1);
#endif
    return wifi_dd;
}


static void set_angle(void *obj, int32_t val)
{
    char buf[128];
    lv_obj_t *bar = (lv_obj_t *)obj;
    lv_bar_set_value(bar, val, LV_ANIM_ON);

    bool running = hw_get_wifi_scanning();

    if (val == 100 || !running) {

        lv_obj_del(lv_obj_get_parent(bar));

        scanning = false;

        vector <wifi_scan_params_t> list;
        hw_get_wifi_scan_result(list);

        // wifi_dd
        lv_dropdown_clear_options(wifi_dd);
        int16_t pos = 0;
        for (auto i : list) {
            lv_dropdown_add_option(wifi_dd, i.ssid.c_str(), pos++);
        }
        lv_dropdown_set_selected(wifi_dd, 0);
        lv_dropdown_get_selected_str(wifi_dd, buf, 128);
        lv_strcpy(wifi_ssid, buf);
    }
}

static void scan_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {

        if (scanning) {
            printf("Scanning !...\n");
            return;
        }

        scanning = true;

        hw_set_wifi_scan();

        lv_obj_t *bar =  ui_create_process_bar(lv_scr_act(), "WiFi Scanning...");
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, set_angle);
        lv_anim_set_time(&a, 20000);
        lv_anim_set_playback_time(&a, 3000);
        lv_anim_set_var(&a, bar);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_start(&a);
    }
}

void ui_wireless_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

    // --- Status section --------------------------------------------------
    status_state_label = create_label(main_page, LV_SYMBOL_WIFI, "Status:",
                                      hw_get_wifi_connected() ? "Connected" : "Disconnected");
    status_ssid_label = create_label(main_page, LV_SYMBOL_WIFI, "SSID:", "N.A");
    status_ip_label = create_label(main_page, LV_SYMBOL_WIFI, "IP:", "N.A");
    status_rssi_label = create_label(main_page, LV_SYMBOL_WIFI, "RSSI:", "N.A");
    status_timer_event_cb(NULL);   ///< fill the rows in immediately, don't wait 2s
    status_timer = lv_timer_create(status_timer_event_cb, 2000, NULL);

    lv_obj_t *con2 = ui_create_option(main_page, LV_SYMBOL_WIFI" Password:", NULL, password_text_crate, NULL);
    lv_obj_t *con1 = ui_create_option(main_page, LV_SYMBOL_WIFI" SSID:", NULL, dropdown_create, NULL);




#ifdef USING_TOUCHPAD
    int w =  lv_disp_get_hor_res(NULL) / 5;
    lv_obj_t *refresh_btn = create_radius_button(main_page, LV_SYMBOL_REFRESH, scan_btn_event, NULL);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *ok_btn = create_radius_button(main_page, LV_SYMBOL_OK, wifi_connect_event, NULL);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -30);

    lv_obj_t *quit_btn = create_radius_button(main_page, LV_SYMBOL_LEFT, [](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, -w, -30);
#else

    lv_obj_t *refresh_btn = create_radius_button(main_page, LV_SYMBOL_REFRESH, scan_btn_event, NULL);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, -40, -20);

    lv_obj_t *ok_btn = create_radius_button(main_page, LV_SYMBOL_OK, wifi_connect_event, NULL);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 40, -20);
#endif



    lv_menu_set_page(menu, main_page);

}


void ui_wireless_exit(lv_obj_t *parent)
{
    status_timer_stop();
}

app_t ui_wireless_main = {
    .setup_func_cb = ui_wireless_enter,
    .exit_func_cb = ui_wireless_exit,
    .user_data = nullptr,
};


