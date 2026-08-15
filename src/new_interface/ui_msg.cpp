/**
 * @file      ui_msg.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * @brief     App-wide notification pop-up and the WiFi connection progress bar.
 *
 * Two things live here, both callable from anywhere (including from hardware
 * callbacks such as the NFC handler in hal_interface.cpp):
 *   - ui_msg_pop_up(), a single-button "OK" dialog,
 *   - ui_show_wifi_process_bar(), the modal progress view shown while joining a
 *     network, which reports success or the specific failure when it finishes.
 *
 * Both suppress low-power/screen blanking while they are on screen -- a dialog
 * the user has not acknowledged should not vanish into a screen timeout.
 */
#include "ui_define.h"

/// The one live pop-up, or NULL. Only one may exist at a time; a second request
/// while one is open is dropped rather than queued.
static lv_obj_t *msgbox = NULL;

/// "Close" handler: tear the dialog down and re-permit low-power mode.
static void msgbox_event(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    printf("msgbox event destroy\n");
    destroy_msgbox(msgbox);
    msgbox = NULL;
    set_low_power_mode_flag(true);
}

/**
 * Show a modal notification with a single "Close" button.
 *
 * The `btns` array is terminated by an empty string, matching create_msgbox()'s
 * expected format. It is `static` because LVGL keeps a pointer to the captions
 * rather than copying them.
 *
 * Silently returns if a pop-up is already open -- so a burst of events (several
 * NFC records from one tag, say) produces one dialog, not a stack of them.
 */
void ui_msg_pop_up(const char *title_txt, const char *msg_txt)
{

    set_low_power_mode_flag(false);

    static const char *btns[] = {"Close", ""};
    if (msgbox) {
        printf("msg box has create!\n");
        return;
    }
    printf("create_msgbox..\n");
    msgbox = create_msgbox(lv_scr_act(), title_txt,
                           msg_txt, btns,
                           msgbox_event, NULL);
}



static lv_timer_t *processBarTimer = NULL;  ///< 500 ms poll driving the progress bar
static uint32_t prevTick = 0;               ///< absolute tick at which the attempt times out

/**
 * Poll the WiFi association attempt, 500 ms per tick.
 *
 * Finishes on either outcome -- a usable connection, or the deadline in
 * `prevTick` passing. Both paths tear down the bar, re-enable input, and raise a
 * pop-up; the failure path inspects hw_get_wifi_status() so the message names
 * the actual problem (wrong password, SSID not found, radio off) instead of a
 * generic error.
 *
 * "Connected" deliberately requires more than hw_get_wifi_connected(): the IP
 * must also be a real address, because association completes before DHCP does
 * and reporting success at that point would show the user an IP of 0.0.0.0.
 *
 * The bar's own progress is cosmetic -- it advances a fixed 5% per tick and is
 * not tied to the connection's actual state.
 */
static void _ui_process_bar_cb(lv_timer_t*t)
{
    lv_obj_t *bar = (lv_obj_t *)lv_timer_get_user_data(t);

    string wifi_ip;
    hw_get_ip_address(wifi_ip);
    bool connected = hw_get_wifi_connected() && wifi_ip != "N.A" &&  wifi_ip != "0.0.0.0";

    if (connected || lv_tick_get() > prevTick) {

        lv_obj_t *cont =  lv_obj_get_parent(bar);
        lv_obj_del(cont);
        lv_timer_del(processBarTimer);
        processBarTimer = NULL;
        enable_input_devices();
        lv_disp_trig_activity(NULL);

        if (connected) {

            string wifi_ssid;
            hw_get_wifi_ssid(wifi_ssid);

            string msg = "Connected " + wifi_ssid + ",IP:" + wifi_ip;
            ui_msg_pop_up("WiFi", msg.c_str());

        } else {

            wl_status_t status = hw_get_wifi_status();
            switch (status) {
#ifdef ARDUINO
#if ESP_IDF_VERSION <ESP_IDF_VERSION_VAL(5,0,0)
            case WL_IDLE_STATUS:
#else
            case WL_STOPPED:
#endif
                ui_msg_pop_up("WiFi", "Connection failed, WIFI is not activated.");
                break;
#endif
            case WL_NO_SSID_AVAIL:
                ui_msg_pop_up("WiFi", "Connection failed. The SSID was not found.");
                break;
            case WL_CONNECT_FAILED:
                ui_msg_pop_up("WiFi", "Connection failed, please check the password.");
                break;
            default:
                ui_msg_pop_up("WiFi", "Connection failed.");
                break;
            }
        }

        set_low_power_mode_flag(true);

        return ;
    }
    int32_t val = lv_bar_get_value(bar) + 5;
    lv_bar_set_value(bar, val, LV_ANIM_ON);
}


/**
 * Show the modal "WiFi connecting..." progress bar and start polling.
 *
 * Input devices are disabled for the duration so the user cannot navigate away
 * mid-attempt; _ui_process_bar_cb() re-enables them on either outcome.
 *
 * The attempt is given 10 seconds. lv_timer_ready() runs the first poll
 * immediately rather than waiting out the initial 500 ms interval.
 */
void ui_show_wifi_process_bar()
{
    if (processBarTimer) {
        printf("Timer is running");
        return;
    }
    prevTick = lv_tick_get() + 10000;   // deadline: 10 s from now
    disable_input_devices();
    lv_obj_t *bar = ui_create_process_bar(lv_scr_act(), "WiFi connecting...");
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    processBarTimer = lv_timer_create(_ui_process_bar_cb, 500, bar);
    lv_timer_ready(processBarTimer);
}
