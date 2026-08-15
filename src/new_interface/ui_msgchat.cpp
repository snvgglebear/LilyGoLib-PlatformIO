/**
 * @file      ui_msgchat.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
/**
 * @brief LoRa chat -- a simple text messenger over the LoRa radio.
 *
 * Broadcast, unencrypted, and unaddressed: every device in range running this
 * app with matching radio settings receives every message. There is no
 * acknowledgement, retry, or delivery guarantee -- a lost packet is simply
 * never seen. Messages are plain text on the air.
 *
 * Text entry differs by board: touch devices get an on-screen LVGL keyboard,
 * while the T-LoRa-Pager reads its physical keyboard through
 * hw_set_keyboard_read_callback().
 *
 * The radio configuration is *shared* with the LoRa test app -- see the
 * `extern radio_params_copy` below -- so whatever was tuned there is what this
 * app transmits on.
 *
 * @see ui_radio.cpp for the settings screen that owns those parameters.
 */
#include "ui_define.h"

#ifdef USING_TOUCHPAD
static lv_obj_t *keyboard = NULL;       ///< on-screen keyboard, touch boards only
#endif

static lv_timer_t *timer = NULL;        ///< polls the radio for incoming messages
static char recv_buf[512];              ///< receive buffer handed to the HAL
static radio_rx_params_t rx_params;
static lv_obj_t *menu = NULL;
static lv_obj_t *quit_btn = NULL;
#define MAX_MSG_COUNT 20                ///< chat history depth; older bubbles are deleted
static lv_obj_t *msg_page;
static lv_obj_t *msg_cont;              ///< scrollable column holding the message bubbles
static lv_obj_t *input_textarea;
static int msg_count = 0;

/// Radio settings owned by ui_radio.cpp. Shared deliberately, so tuning the
/// radio in the test app also retunes this one.
extern radio_params_t radio_params_copy;

// Where the quit button sits, per board -- the round Ultra display and the
// square S3 panel need different placement to keep it clear of the message list.
#if defined(ARDUINO_T_WATCH_S3)
static lv_align_t quit_btn_align = LV_ALIGN_TOP_LEFT;
static int16_t quit_btn_x_ofs = 0;
static int16_t quit_btn_y_ofs = 0;
#elif defined(ARDUINO_T_WATCH_S3_ULTRA)
static lv_align_t quit_btn_align = LV_ALIGN_TOP_MID;
static int16_t quit_btn_x_ofs = 0;
static int16_t quit_btn_y_ofs = 20;
#endif

static void _msg_ta_cb(lv_event_t *e);

/// "HH:MM:SS" for the message timestamp, from the system clock (set over NTP or
/// from the RTC). Returns a pointer to a static buffer -- copy it before the
/// next call.
static char *get_formatted_time(void)
{
    static char time_str[20];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    sprintf(time_str, "%02d:%02d:%02d",
            t->tm_hour,
            t->tm_min,
            t->tm_sec);
    return time_str;
}

/**
 * Back-button handler, doubling as the app's teardown.
 *
 * Root back button -> leave the app: destroy the on-screen keyboard, stop the
 * receive poll, unhook the physical-keyboard callback (leaving it installed
 * would send keystrokes to a deleted text area), restore default radio settings,
 * and return to the launcher.
 *
 * Any other back button -> the user came back from a sub-page, so put the radio
 * into receive mode ready to listen again.
 *
 * lv_obj_del_async() is used for the quit button because this handler may be
 * running *from* that button's own event, and deleting an object mid-event is
 * not safe -- the async form defers it to the end of the LVGL cycle.
 */
static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {
#ifdef USING_TOUCHPAD
        if (keyboard) {
            lv_obj_del(keyboard);
            keyboard = NULL;
        }
#endif
        if (timer) {
            lv_timer_del(timer); timer = NULL;
        }
        hw_set_keyboard_read_callback(NULL);
        hw_set_radio_default();
        lv_obj_clean(menu);
        lv_obj_del(menu);

        disable_keyboard();

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }
        menu_show();
    }

    else {
        hw_feedback();
        radio_params_copy.mode = RADIO_RX;
        hw_set_radio_params(radio_params_copy);
#ifdef USING_TOUCHPAD
        lv_obj_align(quit_btn, quit_btn_align, quit_btn_x_ofs, quit_btn_y_ofs);
#endif
    }
}

/**
 * Append one chat bubble to the message list.
 *
 * Sent and received messages are distinguished the conventional way: sent
 * bubbles are flex-aligned to the right in blue, received ones to the left in
 * grey.
 *
 * @param bg_color  currently unused -- the colour is chosen from `is_send` instead
 */
static void create_msg_label(const char *text, bool is_send, lv_color_t bg_color)
{
    lv_obj_t *msg_row = lv_obj_create(msg_cont);
    lv_obj_set_size(msg_row, lv_obj_get_width(msg_cont), LV_SIZE_CONTENT);
    lv_obj_set_layout(msg_row, LV_LAYOUT_FLEX);
    if (is_send) {
        lv_obj_set_flex_align(msg_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
        lv_obj_set_flex_align(msg_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    lv_obj_set_style_border_width(msg_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(msg_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(msg_row, 5, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(msg_row);
    lv_label_set_text(label, text);

    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    if (is_send) {
        lv_obj_set_style_bg_color(label, lv_color_hex(0x99ccff), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(label, lv_color_hex(0xe6e6e6), LV_PART_MAIN);
    }
    lv_obj_set_style_bg_opa(label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 8, LV_PART_MAIN);

    lv_obj_set_style_max_width(label, lv_obj_get_width(msg_cont) * 7 / 10, LV_PART_MAIN);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
}

/**
 * Send the text area's contents.
 *
 * Note the bubble is added *before* the transmit and is never revoked, so the
 * chat shows what was sent, not what was delivered -- there is no
 * acknowledgement to confirm anything received it.
 *
 * radio_transmit() is the blocking send, so at high spreading factors the UI
 * stalls for the duration of the packet. hw_set_radio_listening() afterwards is
 * essential: transmitting drops the module out of receive mode, and without
 * re-arming it the app would go deaf after the first message.
 *
 * The timestamp prefix is local-only -- only the raw text goes on the air.
 */
static void send_btn_cb(lv_event_t *e)
{
    static  char buf[512];
    const char *text = lv_textarea_get_text(input_textarea);
    if (text[0] == '\0') return;

    // Cap the history: drop the oldest bubble once MAX_MSG_COUNT is reached, so
    // a long conversation cannot exhaust LVGL's object memory.
    if (msg_count >= MAX_MSG_COUNT) {
        lv_obj_t *first_child = lv_obj_get_child(msg_cont, 0);
        if (first_child) {
            lv_obj_del(first_child);
            msg_count--;
        }
    }

    lv_snprintf(buf, sizeof(buf), "%s:%s", get_formatted_time(), text);

    create_msg_label(buf, true, lv_color_hex(0x99ccff));
    msg_count++;

    radio_transmit((const uint8_t *)text, strlen(text));

    lv_textarea_set_text(input_textarea, "");

    lv_obj_scroll_to_y(msg_page, lv_obj_get_y(msg_cont) + lv_obj_get_height(msg_cont), LV_ANIM_ON);

    hw_set_radio_listening();
}

/// Append an incoming message as a left-aligned bubble and scroll to it.
/// Same history cap as send_btn_cb().
void recv_msg(const char *text)
{
    static  char buf[512];
    if (text[0] == '\0') return;
    if (msg_count >= MAX_MSG_COUNT) {
        lv_obj_t *first_child = lv_obj_get_child(msg_cont, 0);
        if (first_child) {
            lv_obj_del(first_child);
            msg_count--;
        }
    }
    lv_snprintf(buf, sizeof(buf), "%s:%s", get_formatted_time(), text);
    create_msg_label(buf, false, lv_color_hex(0xe6e6e6));
    msg_count++;
    lv_obj_scroll_to_y(msg_page, lv_obj_get_y(msg_cont) + lv_obj_get_height(msg_cont), LV_ANIM_ON);
}


/**
 * Text-area event handler, reconciling three very different input styles.
 *
 * The branching on lv_indev_active() exists because the same text area has to
 * behave sensibly whichever device the user is holding:
 *   - POINTER (touch): tapping the field pops up the on-screen keyboard and puts
 *     the group into edit mode; LV_EVENT_READY/DEFOCUSED hides it again.
 *   - ENCODER: there is no keyboard to show. A click toggles the group in and out
 *     of edit mode, which is what switches the encoder between "move focus
 *     between widgets" and "act on this widget".
 *   - KEYPAD (physical keyboard): returns early -- keystrokes go straight into
 *     the text area and none of this handling applies.
 *
 * The LV_KEY_ENTER case deletes a character because the encoder's centre press
 * arrives as Enter, and a newline in a chat field is not wanted; this makes it
 * behave as backspace instead. lv_event_stop_processing() prevents LVGL's
 * default Enter handling from also running.
 */
static void _msg_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool state =  lv_obj_has_state(ta, LV_STATE_FOCUSED);
    bool edited =  lv_obj_has_state(ta, LV_STATE_EDITED);

    lv_indev_t *indev =   lv_indev_active();
    if (indev == NULL) {
        return;
    }

    if (indev->type == LV_INDEV_TYPE_ENCODER) {
    } else if (indev->type == LV_INDEV_TYPE_POINTER) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            hw_feedback();
        }
    } else if (indev->type == LV_INDEV_TYPE_KEYPAD) {
        return ;
    }

    if (code == LV_EVENT_KEY) {
        lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
        if (key == LV_KEY_ENTER) {
            lv_textarea_delete_char(input_textarea);
            lv_event_stop_processing(e);
        }
    }

#ifdef USING_TOUCHPAD
    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
#endif

    if (code == LV_EVENT_CLICKED && indev->type == LV_INDEV_TYPE_POINTER) {
        lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), true);
#ifdef USING_TOUCHPAD
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(quit_btn, quit_btn_align, quit_btn_x_ofs, quit_btn_y_ofs);
#endif
    } else  if (code == LV_EVENT_CLICKED && indev->type == LV_INDEV_TYPE_ENCODER) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            enable_keyboard();
        }
    }
}


void create_chat_ui(lv_obj_t *parent)
{
    lv_obj_t *main_cont = lv_obj_create(parent);
    lv_obj_set_size(main_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(main_cont, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(main_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(main_cont, 0, LV_PART_MAIN);

    msg_page = lv_obj_create(main_cont);
    lv_obj_set_size(msg_page, lv_pct(95), lv_pct(68));
    lv_obj_align(msg_page, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_border_width(msg_page, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(msg_page, 5, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(msg_page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(msg_page, 0, LV_PART_MAIN);

    msg_cont = lv_obj_create(msg_page);
    lv_obj_set_size(msg_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(msg_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(msg_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_cont, 0, LV_PART_MAIN);

    lv_obj_set_scroll_dir(msg_cont, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(msg_cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *input_cont = lv_obj_create(main_cont);
    lv_obj_set_style_pad_all(input_cont, 0, LV_PART_MAIN);
    lv_obj_set_size(input_cont, lv_pct(95), lv_pct(20));
    lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(input_cont, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(input_cont, 0, LV_PART_MAIN);
    lv_obj_set_layout(input_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_radius(input_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input_cont, 0, LV_PART_MAIN);

    input_textarea = lv_textarea_create(input_cont);
    lv_obj_set_size(input_textarea, lv_pct(70), lv_pct(100));
    lv_textarea_set_placeholder_text(input_textarea, "Please enter your message...");
    lv_obj_set_style_radius(input_textarea, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(input_textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(input_textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(input_textarea, lv_color_white(), LV_PART_MAIN);
    lv_textarea_set_max_length(input_textarea, 500);
    lv_obj_set_scroll_dir(input_textarea, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(input_textarea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(input_textarea, _msg_ta_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *send_btn = lv_btn_create(input_cont);
    lv_obj_set_size(send_btn, lv_pct(10), lv_pct(100));
    lv_obj_set_style_radius(send_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(send_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(send_btn, send_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_all(send_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(send_btn, 1, LV_PART_MAIN);
    lv_obj_t *send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, LV_SYMBOL_GPS);
    lv_obj_center(send_label);
    lv_obj_set_style_text_color(send_label, lv_color_black(), LV_PART_MAIN);


    lv_obj_t *setting_btn = lv_btn_create(input_cont);
    lv_obj_set_size(setting_btn, lv_pct(10), lv_pct(100));
    lv_obj_set_style_radius(setting_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(setting_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(setting_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(setting_btn, 1, LV_PART_MAIN);
    lv_obj_t *setting_label = lv_label_create(setting_btn);
    lv_label_set_text(setting_label, LV_SYMBOL_SETTINGS);
    lv_obj_center(setting_label);
    lv_obj_set_style_text_color(setting_label, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *section;
    lv_obj_t *sub_rf_setting_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sub_rf_setting_page, lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), LV_PART_MAIN), 0);
    section = lv_menu_section_create(sub_rf_setting_page);
    lv_menu_set_load_page_event(menu, setting_btn, sub_rf_setting_page);


    // The radio-settings sub-page reuses the dropdown builders from ui_radio.cpp
    // rather than duplicating them, which is also why radio_params_copy is
    // shared between the two apps.
    extern lv_obj_t *create_cr_dropdown(lv_obj_t *parent);
    extern lv_obj_t *create_sf_dropdown(lv_obj_t *parent);
    extern lv_obj_t *create_tx_power_dropdown(lv_obj_t *parent);
    extern lv_obj_t *create_bandwidth_dropdown(lv_obj_t *parent);
    extern lv_obj_t *create_frequency_dropdown(lv_obj_t *parent);
    extern lv_obj_t *create_syncword_textarea(lv_obj_t *parent);

    ui_create_option(section, "Frequency:", NULL, create_frequency_dropdown, NULL);
    ui_create_option(section, "Bandwidth:", NULL, create_bandwidth_dropdown, NULL);
    ui_create_option(section, "TX Power:", NULL, create_tx_power_dropdown, NULL);
    ui_create_option(section, "Coding rate:", NULL, create_cr_dropdown, NULL);
    ui_create_option(section, "Spreading factor:", NULL, create_sf_dropdown, NULL);
    ui_create_option(section, "SyncWord:", NULL, create_syncword_textarea, NULL);
}

/**
 * Poll the radio for an incoming message, every 300 ms.
 *
 * hw_get_radio_rx() is cheap when nothing has arrived (it waits ~2 ms on the
 * IRQ flag and returns), so polling this often is fine. On a message: display
 * it, vibrate, and play a notification chime from the FFat partition.
 *
 * @note The NUL is written at `recv_buf[rx_params.length + 1]`, one byte beyond
 *       the payload's end rather than at it. That leaves the byte at
 *       `[rx_params.length]` uninitialised, so the string handed to recv_msg()
 *       may carry a stray character from a previous, longer packet before it
 *       terminates. `recv_buf` is 512 bytes against a 255-byte maximum LoRa
 *       packet, so the write itself stays in bounds.
 */
static void msg_chat_receiver_task(lv_timer_t *t)
{
    rx_params.data = (uint8_t *)recv_buf;
    rx_params.length = sizeof(recv_buf);
    hw_get_radio_rx(rx_params);
    if (rx_params.state == 0 && rx_params.length != 0) {
        recv_buf[rx_params.length + 1] = '\0';
        recv_msg(recv_buf);
        hw_feedback();
        hw_set_volume(70);
        hw_set_sd_music_play(AUDIO_SOURCE_FATFS, "/notification.mp3");
    }
}

/**
 * Build the chat screen and start listening.
 *
 * The radio is put into RX mode immediately, so messages arriving while the user
 * is still reading are captured. The on-screen keyboard is created up front but
 * hidden, and revealed by _msg_ta_cb() when the text field is tapped.
 */
void ui_msgchat_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);

    if (!hw_get_lora_enabled()) {
        lv_obj_t *main_page = lv_menu_page_create(menu, NULL);
        lv_obj_t *cont = lv_menu_cont_create(main_page);
        lv_obj_remove_style_all(cont);
        lv_obj_set_size(cont, lv_pct(100), 80);
        lv_obj_t *label = lv_label_create(cont);
        lv_label_set_text(label, "LoRa is off -- enable it in Settings");
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_set_width(label, lv_pct(90));
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_menu_set_page(menu, main_page);
        return;
    }

    hw_get_radio_params(radio_params_copy);
    radio_params_copy.mode = RADIO_RX;
    hw_set_radio_params(radio_params_copy);

    lv_obj_t *sub_mechanics_page = lv_menu_page_create(menu, NULL);

    create_chat_ui(sub_mechanics_page);

    lv_menu_set_page(menu, sub_mechanics_page);

#ifdef USING_TOUCHPAD
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event(keyboard, [](lv_event_t * e) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(quit_btn, quit_btn_align, quit_btn_x_ofs, quit_btn_y_ofs);
    }, LV_EVENT_READY, NULL);

    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
    lv_obj_align(quit_btn, quit_btn_align, quit_btn_x_ofs, quit_btn_y_ofs);
#endif

    timer = lv_timer_create(msg_chat_receiver_task, 300, NULL);
}


/// Empty: teardown happens in back_event_handler(), the only exit from the screen.
void ui_msgchat_exit(lv_obj_t *parent)
{

}

/// Registered on the launcher by ui_main.cpp.
app_t ui_msgchat_main = {
    .setup_func_cb = ui_msgchat_enter,
    .exit_func_cb = ui_msgchat_exit,
    .user_data = nullptr,
};

