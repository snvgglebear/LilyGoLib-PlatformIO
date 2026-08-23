/**
 * @file      wifi_page.cpp
 * @license   MIT
 * @brief     The Wi-Fi settings subpage. See wifi_page.h.
 */
#include "wifi_page.h"

#include "app_settings.h"
#include "settings_widgets.h"

#include "../app_config.h"
#include "../wifi/wifi_service.h"

#include <usable_area.h>

#include <stdio.h>
#include <string.h>

namespace
{

/// How often the live rows re-read wifi_service. Twice a second rather than
/// once: this page is where the user watches a connection succeed or fail, and
/// a second of lag on "Connecting..." -> "Wrong password" reads as a hang.
constexpr uint32_t REFRESH_MS = 500;

lv_obj_t *s_enable_switch = nullptr;
lv_obj_t *s_status_value  = nullptr;
lv_obj_t *s_network_value = nullptr;
lv_obj_t *s_ip_value      = nullptr;
lv_obj_t *s_signal_value  = nullptr;
lv_obj_t *s_scan_button   = nullptr;
lv_obj_t *s_forget_button = nullptr;
lv_obj_t *s_list          = nullptr;
lv_timer_t *s_timer       = nullptr;

/*The password prompt, which outlives the click that opened it.*/
lv_obj_t *s_prompt   = nullptr;
lv_obj_t *s_password_ta = nullptr;
lv_obj_t *s_keyboard = nullptr;
char s_prompt_ssid[WIFI_SSID_MAX] = {0};

/// Set while the list is being rebuilt, so drawing it does not look like a
/// finished scan and trigger another rebuild on the next tick.
int s_shown_scan_count = -1;

const char *barsText(int bars)
{
    switch (bars) {
    case 4:  return "excellent";
    case 3:  return "good";
    case 2:  return "fair";
    case 1:  return "weak";
    default: return "--";
    }
}

// -- password prompt -------------------------------------------------------

void closePrompt()
{
    // Keyboard first: it is parented to lv_layer_top(), not to the msgbox, so
    // closing the box alone would leave it on screen over whatever came next.
    if (s_keyboard) {
        lv_obj_delete(s_keyboard);
        s_keyboard = nullptr;
    }
    if (s_prompt) {
        lv_msgbox_close(s_prompt);
        s_prompt = nullptr;
    }
    s_password_ta = nullptr;
}

void promptCancelClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    closePrompt();
}

void promptConnectClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_password_ta) {
        return;
    }
    const char *password = lv_textarea_get_text(s_password_ta);

    /*Covers the empty case too, not just a short one: openPrompt() is only
      reached for a secured network -- an open one connects straight from the
      list without asking -- so there is no passphrase shorter than WPA2's
      eight characters that could ever be the right answer here. Without this,
      an empty box would produce an attempt the AP refuses and a "Wrong
      password" about a passphrase the user never typed.*/
    if (!password || strlen(password) < 8) {
        lv_textarea_set_placeholder_text(s_password_ta, "at least 8 characters");
        lv_textarea_set_text(s_password_ta, "");
        return;
    }
    wifi_service_connect(s_prompt_ssid, password);   // non-null and >= 8 chars by here
    closePrompt();
}

/// Typing "OK" on the keyboard means the same as tapping Connect.
void keyboardReadyCb(lv_event_t *e)
{
    LV_UNUSED(e);
    promptConnectClicked(nullptr);
}

void openPrompt(const char *ssid)
{
    closePrompt();      // never two at once
    strncpy(s_prompt_ssid, ssid, sizeof(s_prompt_ssid) - 1);
    s_prompt_ssid[sizeof(s_prompt_ssid) - 1] = '\0';

    s_prompt = lv_msgbox_create(NULL);
    lv_obj_set_width(s_prompt, usable_area_screen_width() - 2 * SAFE_INSET);
    lv_msgbox_add_title(s_prompt, s_prompt_ssid);

    s_password_ta = lv_textarea_create(lv_msgbox_get_content(s_prompt));
    lv_obj_set_width(s_password_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "password");
    lv_obj_set_style_text_font(s_password_ta, APP_FONT_BODY, 0);

    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_prompt, "Connect"),
                        promptConnectClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_prompt, "Cancel"),
                        promptCancelClicked, LV_EVENT_CLICKED, NULL);

    // The prompt goes to the top and the keyboard takes the bottom, rather
    // than the msgbox sitting centred with the keyboard over it. Both are
    // inset by SAFE_INSET so neither runs under the curved glass.
    lv_obj_align(s_prompt, LV_ALIGN_TOP_MID, 0, SAFE_INSET);

    s_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_keyboard, usable_area_screen_width() - 2 * SAFE_INSET,
                    LV_PCT(APP_WIFI_KEYBOARD_HEIGHT_PCT));
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -SAFE_INSET);
    lv_keyboard_set_textarea(s_keyboard, s_password_ta);
    lv_obj_add_event_cb(s_keyboard, keyboardReadyCb, LV_EVENT_READY, NULL);
}

// -- scan list -------------------------------------------------------------

void networkClicked(lv_event_t *e)
{
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    const WifiScanEntry *entry = wifi_service_scan_entry(index);
    if (!entry) {
        return;     // the list was rebuilt under the tap
    }
    if (wifi_hal_entry_is_open(*entry)) {
        wifi_service_connect(entry->ssid, "");   // nothing to ask for
        return;
    }
    openPrompt(entry->ssid);
}

void rebuildList()
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);

    const int count = wifi_service_scan_count();
    s_shown_scan_count = count;

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_obj_set_style_text_font(empty, APP_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(empty, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(empty, wifi_service_enabled() ? "No networks found"
                                                        : "Turn Wi-Fi on to scan");
        return;
    }

    char detail[48];
    for (int i = 0; i < count; ++i) {
        const WifiScanEntry *entry = wifi_service_scan_entry(i);
        lv_obj_t *button = lv_list_add_button(s_list, LV_SYMBOL_WIFI, entry->ssid);
        lv_obj_set_style_text_font(button, APP_FONT_BODY, 0);
        lv_obj_add_event_cb(button, networkClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        // Bars are computed from this entry's own RSSI rather than through
        // wifi_service_bars(), which reports the *association's* strength and
        // would give every row in the list the same number.
        int bars = 1;
        if (entry->rssi >= APP_WIFI_RSSI_EXCELLENT)   bars = 4;
        else if (entry->rssi >= APP_WIFI_RSSI_GOOD)   bars = 3;
        else if (entry->rssi >= APP_WIFI_RSSI_FAIR)   bars = 2;

        snprintf(detail, sizeof(detail), "%s  %s", barsText(bars),
                 wifi_hal_entry_is_open(*entry) ? "open" : "secured");
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, APP_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(label, detail);
    }
}

// -- live rows -------------------------------------------------------------

void refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    char buf[48];

    const bool on = wifi_service_enabled();

    if (s_status_value) {
        lv_label_set_text(s_status_value, wifi_service_status_text());
    }
    if (s_network_value) {
        const char *ssid = wifi_service_ssid();
        if (ssid[0]) {
            lv_label_set_text(s_network_value, ssid);
        } else if (wifi_service_has_saved()) {
            // Saved but not joined -- worth distinguishing from having nothing
            // saved at all, since only one of the two explains a retry loop.
            snprintf(buf, sizeof(buf), "%s (saved)", wifi_service_saved_ssid());
            lv_label_set_text(s_network_value, buf);
        } else {
            lv_label_set_text(s_network_value, "none");
        }
    }
    if (s_ip_value) {
        const char *ip = wifi_service_ip();
        lv_label_set_text(s_ip_value, ip[0] ? ip : "--");
    }
    if (s_signal_value) {
        const int bars = wifi_service_bars();
        if (bars == 0) {
            lv_label_set_text(s_signal_value, "--");
        } else {
            snprintf(buf, sizeof(buf), "%s  (%d dBm)", barsText(bars),
                     (int)wifi_service_rssi());
            lv_label_set_text(s_signal_value, buf);
        }
    }
    if (s_enable_switch) {
        // The tray's Wi-Fi tile can be opened over this page and toggled, so
        // the switch is a view of the service rather than the record of what
        // it was last set to. lv_obj_set_state() sends no LV_EVENT_VALUE_CHANGED,
        // so this cannot re-enter enableChanged().
        lv_obj_set_state(s_enable_switch, LV_STATE_CHECKED, on);
    }
    if (s_forget_button) {
        lv_obj_set_state(s_forget_button, LV_STATE_DISABLED, !wifi_service_has_saved());
    }
    if (s_scan_button) {
        // Disabled rather than hidden while scanning or off, so the row does
        // not move under the finger.
        const bool usable = on && !wifi_service_scanning();
        lv_obj_set_state(s_scan_button, LV_STATE_DISABLED, !usable);
    }

    // The scan finished (or the results were cleared) since the list was last
    // drawn. wifi_service's listener could push this instead, but the page is
    // rebuilt on every visit and a stale callback firing at a freed list is a
    // worse failure than half a second of lag.
    if (!wifi_service_scanning() && wifi_service_scan_count() != s_shown_scan_count) {
        rebuildList();
    }
}

// -- controls --------------------------------------------------------------

void enableChanged(lv_event_t *e)
{
    const bool on = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    wifi_service_set_enabled(on);
    if (on) {
        wifi_service_scan();    // the reason to turn it on is almost always to join something
    } else {
        s_shown_scan_count = -1;
        rebuildList();
    }
    if (s_timer) {
        lv_timer_ready(s_timer);    // repaint the rows now rather than in half a second
    }
}

void scanClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_service_scan();
    if (s_timer) {
        lv_timer_ready(s_timer);
    }
}

void forgetClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_service_forget();
    if (s_timer) {
        lv_timer_ready(s_timer);
    }
}

} // namespace

void settings_wifi_page_build(lv_obj_t *page)
{
    settings_wifi_page_stop();      // a second visit must not inherit the first's timer

    s_enable_switch = settings_switch(page, LV_SYMBOL_WIFI, "Wi-Fi",
                                      wifi_service_enabled(), enableChanged, NULL);

    s_status_value  = settings_value(page, NULL, "Status", "--");
    s_network_value = settings_value(page, NULL, "Network", "--");
    s_ip_value      = settings_value(page, NULL, "IP address", "--");
    s_signal_value  = settings_value(page, NULL, "Signal", "--");

    s_scan_button = settings_button(page, LV_SYMBOL_REFRESH, "Scan for networks",
                                    "Scan", scanClicked, NULL);
    s_forget_button = settings_button(page, LV_SYMBOL_TRASH, "Saved network", "Forget",
                                      forgetClicked, NULL);

    settings_section(page, "Networks");

    s_list = lv_list_create(page);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_height(s_list, APP_WIFI_LIST_HEIGHT);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);

    s_shown_scan_count = -1;
    rebuildList();

    s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
    lv_timer_ready(s_timer);        // fill the rows now, not in half a second
}

void settings_wifi_page_stop(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    closePrompt();
    s_enable_switch = s_status_value = s_network_value = nullptr;
    s_ip_value = s_signal_value = s_scan_button = s_forget_button = s_list = nullptr;
    s_shown_scan_count = -1;
}
