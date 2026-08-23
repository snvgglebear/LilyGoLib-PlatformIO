/**
 * @file      wifi_hal.cpp
 * @license   MIT
 * @brief     Wi-Fi platform shim. See wifi_hal.h.
 */
#include "wifi_hal.h"

#include "../app_config.h"

#include <lvgl.h>       // LV_UNUSED, and lv_tick_*() in the native branch
#include <string.h>

// Arduino/ESP-IDF headers at file scope, never inside the anonymous namespace
// below -- see the long note in settings/app_settings.cpp about what including
// <Preferences.h> from inside an unnamed namespace does to ::std on the
// xtensa-esp32s3 toolchain. <WiFi.h> pulls in the same core headers.
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <LilyGoLib.h>
#else
#include <stdio.h>
#endif

namespace
{

bool s_radio_on = false;

#ifdef ARDUINO

/**
 * SNTP "clock stepped" notification. The ESP32's system clock is volatile, so
 * the corrected time is pushed down into the battery-backed RTC -- lifted from
 * src/factory/factory.ino's time_available(), including the probe test, since
 * writing to an RTC that never answered would just fail silently.
 *
 * Runs on the SNTP task, not the LVGL task. It touches `instance` and nothing
 * else; no LVGL call belongs here.
 */
void onTimeSynced(struct timeval *tv)
{
    LV_UNUSED(tv);
    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        instance.rtc.hwClockWrite();
    }
    Serial.println("[wifi] system clock synchronised from NTP");
}

/**
 * Association got us an IP. Starting SNTP is deferred to here rather than done
 * at connect time because configTime() begins DNS resolution immediately, and
 * that needs a usable interface.
 *
 * Also off-task. Same rule as above.
 */
void onGotIp(WiFiEvent_t event, WiFiEventInfo_t info)
{
    LV_UNUSED(event);
    Serial.print("[wifi] got IP ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
#if APP_WIFI_NTP_SYNC
    configTime(APP_NTP_GMT_OFFSET_SEC, APP_NTP_DAYLIGHT_OFFSET_SEC,
               APP_NTP_SERVER_PRIMARY, APP_NTP_SERVER_SECONDARY);
#endif
}

#else  // !ARDUINO -- native/SDL2 emulator build

/*[SIM] There is no radio on the host. Rather than factory's approach -- one
  invented AP and a connect that never completes -- this fakes the whole
  sequence on a timer, so the settings page, the status bar and the tray can
  all be driven through their real states in the emulator.*/

constexpr uint32_t SIM_SCAN_MS    = 1500;
constexpr uint32_t SIM_CONNECT_MS = 2000;

/// The password the simulated AP accepts, so the wrong-password path is
/// reachable in the emulator instead of only on hardware.
constexpr const char *SIM_PASSWORD = "password";

const WifiScanEntry SIM_NETWORKS[] = {
    {"LilyGo-AABB0",   -42, 3, 6},
    {"Snvgglebear",    -58, 3, 11},
    {"guest",          -71, 0, 1},     // authmode 0 == open
    {"neighbour-2.4",  -84, 3, 3},
};

uint32_t s_scan_started_ms = 0;
bool     s_scanning = false;

uint32_t s_connect_started_ms = 0;
WifiHalStatus s_status = WIFI_HAL_OFF;
char s_ssid[WIFI_SSID_MAX] = {0};
char s_pending_password[WIFI_PASSWORD_MAX] = {0};

/// Advances the simulated connect. Called from every status read, so the fake
/// progresses on whatever polls it rather than needing its own tick hook.
void simTick()
{
    if (s_status == WIFI_HAL_CONNECTING &&
        lv_tick_elaps(s_connect_started_ms) >= SIM_CONNECT_MS) {
        s_status = (strcmp(s_pending_password, SIM_PASSWORD) == 0)
                   ? WIFI_HAL_CONNECTED : WIFI_HAL_WRONG_PASSWORD;
    }
}

#endif // ARDUINO

} // namespace

bool wifi_hal_entry_is_open(const WifiScanEntry &entry)
{
    return entry.authmode == 0;
}

void wifi_hal_begin()
{
#ifdef ARDUINO
    sntp_set_time_sync_notification_cb(onTimeSynced);
    WiFi.onEvent(onGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // Both off deliberately -- see the header. wifi_service.cpp decides when to
    // associate and holds the only copy of the credentials.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
#endif
    s_radio_on = false;
}

void wifi_hal_radio(bool on)
{
    if (on == s_radio_on) {
        return;
    }
    s_radio_on = on;
#ifdef ARDUINO
    if (on) {
        WiFi.mode(WIFI_STA);
    } else {
        WiFi.disconnect(true);   // true: drop the association *and* the IDF's stored copy
        WiFi.mode(WIFI_OFF);
    }
#else
    s_status = on ? WIFI_HAL_IDLE : WIFI_HAL_OFF;
    if (!on) {
        s_scanning = false;
        s_ssid[0] = '\0';
    }
#endif
}

bool wifi_hal_radio_on()
{
    return s_radio_on;
}

void wifi_hal_scan_start()
{
    if (!s_radio_on) {
        return;
    }
#ifdef ARDUINO
    WiFi.scanDelete();          // drop the previous results, or the new scan is refused
    WiFi.scanNetworks(true);    // true: asynchronous
#else
    s_scanning = true;
    s_scan_started_ms = lv_tick_get();
#endif
}

bool wifi_hal_scan_running()
{
#ifdef ARDUINO
    return WiFi.scanComplete() == WIFI_SCAN_RUNNING;
#else
    if (s_scanning && lv_tick_elaps(s_scan_started_ms) >= SIM_SCAN_MS) {
        s_scanning = false;
    }
    return s_scanning;
#endif
}

int wifi_hal_scan_collect(WifiScanEntry *out, int max_entries)
{
    if (!out || max_entries <= 0) {
        return 0;
    }
#ifdef ARDUINO
    // Negative is a sentinel, not a count: WIFI_SCAN_RUNNING while in flight,
    // WIFI_SCAN_FAILED if none was ever started. Either way there is nothing
    // to copy, which is not an error -- the caller polls until entries appear.
    const int16_t found = WiFi.scanComplete();
    if (found <= 0) {
        return 0;
    }

    int written = 0;
    for (int16_t i = 0; i < found && written < max_entries; ++i) {
        String ssid;
        uint8_t encryption;
        int32_t rssi;
        uint8_t *bssid;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, encryption, rssi, bssid, channel);

        // A hidden AP scans as an empty SSID, which cannot be joined from a
        // list and would show as a blank row. Skip rather than render it.
        if (ssid.length() == 0) {
            continue;
        }
        WifiScanEntry &e = out[written++];
        strncpy(e.ssid, ssid.c_str(), WIFI_SSID_MAX - 1);
        e.ssid[WIFI_SSID_MAX - 1] = '\0';
        e.rssi = rssi;
        e.authmode = encryption;
        e.channel = (uint8_t)channel;
    }
    return written;
#else
    const int count = (int)(sizeof(SIM_NETWORKS) / sizeof(SIM_NETWORKS[0]));
    const int written = count < max_entries ? count : max_entries;
    memcpy(out, SIM_NETWORKS, (size_t)written * sizeof(WifiScanEntry));
    return written;
#endif
}

void wifi_hal_connect(const char *ssid, const char *password)
{
    if (!s_radio_on || !ssid) {
        return;
    }
#ifdef ARDUINO
    // A scan holds the radio and makes begin() fail; drop any results first.
    WiFi.scanDelete();
    WiFi.begin(ssid, password && password[0] ? password : nullptr);
#else
    strncpy(s_ssid, ssid, WIFI_SSID_MAX - 1);
    s_ssid[WIFI_SSID_MAX - 1] = '\0';
    strncpy(s_pending_password, password ? password : "", WIFI_PASSWORD_MAX - 1);
    s_pending_password[WIFI_PASSWORD_MAX - 1] = '\0';
    s_status = WIFI_HAL_CONNECTING;
    s_connect_started_ms = lv_tick_get();
#endif
}

void wifi_hal_disconnect()
{
#ifdef ARDUINO
    WiFi.disconnect(false);   // false: stay in STA mode, just drop the association
#else
    s_ssid[0] = '\0';
    s_status = s_radio_on ? WIFI_HAL_IDLE : WIFI_HAL_OFF;
#endif
}

bool wifi_hal_connected()
{
    return wifi_hal_status() == WIFI_HAL_CONNECTED;
}

WifiHalStatus wifi_hal_status()
{
    if (!s_radio_on) {
        return WIFI_HAL_OFF;
    }
#ifdef ARDUINO
    switch (WiFi.status()) {
    case WL_CONNECTED:       return WIFI_HAL_CONNECTED;
    case WL_IDLE_STATUS:     return WIFI_HAL_CONNECTING;
    case WL_NO_SSID_AVAIL:   return WIFI_HAL_NO_AP_FOUND;
    case WL_CONNECT_FAILED:  return WIFI_HAL_WRONG_PASSWORD;
    case WL_CONNECTION_LOST: return WIFI_HAL_CONNECT_FAILED;
    case WL_DISCONNECTED:    return WIFI_HAL_IDLE;
    default:                 return WIFI_HAL_IDLE;
    }
#else
    simTick();
    return s_status;
#endif
}

int32_t wifi_hal_rssi()
{
#ifdef ARDUINO
    return WiFi.isConnected() ? WiFi.RSSI() : 0;
#else
    return wifi_hal_status() == WIFI_HAL_CONNECTED ? -55 : 0;
#endif
}

void wifi_hal_ssid(char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        strncpy(out, WiFi.SSID().c_str(), len - 1);
        out[len - 1] = '\0';
    }
#else
    if (wifi_hal_status() == WIFI_HAL_CONNECTED) {
        strncpy(out, s_ssid, len - 1);
        out[len - 1] = '\0';
    }
#endif
}

void wifi_hal_ip(char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        strncpy(out, WiFi.localIP().toString().c_str(), len - 1);
        out[len - 1] = '\0';
    }
#else
    if (wifi_hal_status() == WIFI_HAL_CONNECTED) {
        strncpy(out, "192.168.1.42", len - 1);
        out[len - 1] = '\0';
    }
#endif
}
