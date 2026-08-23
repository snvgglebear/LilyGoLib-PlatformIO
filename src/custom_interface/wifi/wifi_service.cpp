/**
 * @file      wifi_service.cpp
 * @license   MIT
 * @brief     Wi-Fi state, credentials and auto-connect. See wifi_service.h.
 */
#include "wifi_service.h"

#include "../app_config.h"
#include "../settings/app_settings.h"

#include <lvgl.h>       // lv_tick_get()/lv_tick_elaps(), on both builds
#include <string.h>

// At file scope, outside the anonymous namespace -- see the note in
// settings/app_settings.cpp about including Arduino core headers from inside
// an unnamed namespace.
#ifdef ARDUINO
#include <Preferences.h>
#else
#include <stdio.h>
#endif

namespace
{

// -- state -----------------------------------------------------------------

char s_saved_ssid[WIFI_SSID_MAX] = {0};
char s_saved_password[WIFI_PASSWORD_MAX] = {0};

char s_ssid_buf[WIFI_SSID_MAX] = {0};
char s_ip_buf[16] = {0};        ///< "255.255.255.255" + NUL

WifiScanEntry s_scan[APP_WIFI_MAX_SCAN_RESULTS];
int s_scan_count = 0;
bool s_scanning = false;

WifiHalStatus s_last_status = WIFI_HAL_OFF;

uint32_t s_connect_started_ms = 0;
bool s_connecting = false;

/*Reconnect timer. Held as "pending, since this tick" and tested with
  lv_tick_elaps() rather than as a deadline compared with lv_tick_get():
  lv_tick_get() wraps at 2^32 ms (~49 days), and a deadline that wrapped would
  either fire instantly or never. lv_tick_elaps() does the wrap-safe
  subtraction. The watch is expected to run for months between reboots.*/
bool s_retry_pending = false;
uint32_t s_retry_from_ms = 0;

/// Latched when the AP rejects the passphrase. Auto-connect stays off until
/// the user supplies a new one -- see wifi_service_connect().
bool s_password_rejected = false;

constexpr int MAX_LISTENERS = 4;
WifiListener s_listeners[MAX_LISTENERS] = {nullptr};

void notifyListeners()
{
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (s_listeners[i]) {
            s_listeners[i]();
        }
    }
}

// -- credential storage ----------------------------------------------------

#ifdef ARDUINO

constexpr const char *NVS_NAMESPACE = "wifi_creds";
constexpr const char *NVS_KEY_SSID  = "ssid";
constexpr const char *NVS_KEY_PASS  = "pass";

void credentialsLoad()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly*/ true)) {
        return;   // namespace does not exist yet: nothing saved, not an error
    }
    prefs.getString(NVS_KEY_SSID, s_saved_ssid, sizeof(s_saved_ssid));
    prefs.getString(NVS_KEY_PASS, s_saved_password, sizeof(s_saved_password));
    prefs.end();
}

void credentialsSave()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly*/ false)) {
        return;
    }
    prefs.putString(NVS_KEY_SSID, s_saved_ssid);
    prefs.putString(NVS_KEY_PASS, s_saved_password);
    prefs.end();
}

void credentialsErase()
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, /*readOnly*/ false)) {
        prefs.clear();
        prefs.end();
    }
}

#else // !ARDUINO -- native/SDL2 emulator build

/*A file beside the settings blob, for the same reason app_settings.cpp keeps
  one: so the load/save path is exercised in the emulator rather than being the
  one part of the module that only ever runs on hardware.*/
constexpr const char *CREDENTIALS_PATH = "custom_interface_wifi.txt";

void credentialsLoad()
{
    FILE *f = fopen(CREDENTIALS_PATH, "r");
    if (!f) {
        return;
    }
    if (!fgets(s_saved_ssid, sizeof(s_saved_ssid), f)) {
        s_saved_ssid[0] = '\0';
    }
    if (!fgets(s_saved_password, sizeof(s_saved_password), f)) {
        s_saved_password[0] = '\0';
    }
    fclose(f);
    // fgets keeps the newline; the SSID "home\n" is not the SSID "home".
    s_saved_ssid[strcspn(s_saved_ssid, "\r\n")] = '\0';
    s_saved_password[strcspn(s_saved_password, "\r\n")] = '\0';
}

void credentialsSave()
{
    FILE *f = fopen(CREDENTIALS_PATH, "w");
    if (!f) {
        printf("[wifi] could not write %s\n", CREDENTIALS_PATH);
        return;
    }
    fprintf(f, "%s\n%s\n", s_saved_ssid, s_saved_password);
    fclose(f);
}

void credentialsErase()
{
    remove(CREDENTIALS_PATH);
}

#endif // ARDUINO

// -- helpers ---------------------------------------------------------------

/// Begin an association with the saved network, if there is one to use.
void startSavedConnect()
{
    if (!s_saved_ssid[0] || !wifi_hal_radio_on() || s_password_rejected) {
        return;
    }
    wifi_hal_connect(s_saved_ssid, s_saved_password);
    s_connecting = true;
    s_connect_started_ms = lv_tick_get();
    s_retry_pending = false;
}

void scheduleRetry()
{
    s_connecting = false;
    // A rejected passphrase is not retried: the AP has told us the answer and
    // it will not change until the user types a different one.
    s_retry_pending = !s_password_rejected;
    s_retry_from_ms = lv_tick_get();
}

} // namespace

// -- radio -----------------------------------------------------------------

bool wifi_service_enabled()
{
    return wifi_hal_radio_on();
}

void wifi_service_set_enabled(bool enable)
{
    if (enable == wifi_hal_radio_on()) {
        return;
    }
    wifi_hal_radio(enable);

    if (enable) {
        // A fresh switch-on is the user asking again, so an earlier rejection
        // should not keep auto-connect muted.
        s_password_rejected = false;
        startSavedConnect();
    } else {
        s_connecting = false;
        s_retry_pending = false;
        s_scanning = false;
        s_scan_count = 0;
    }

    app_settings_set_wifi_enabled(enable);
    s_last_status = wifi_hal_status();
    notifyListeners();
}

// -- state -----------------------------------------------------------------

WifiHalStatus wifi_service_status()
{
    const WifiHalStatus status = wifi_hal_status();

    /*Arduino reports WL_DISCONNECTED for the whole of an attempt as well as
      after one gives up, and wifi_hal maps that to WIFI_HAL_IDLE because from
      the radio's point of view the two really are the same. This module is the
      one that knows an attempt is in flight, so it is the one that can tell
      them apart -- without this the UI reads "Not connected" for the entire
      fifteen seconds a connect takes, then flips straight to "Connected".*/
    if (s_connecting && status != WIFI_HAL_CONNECTED && status != WIFI_HAL_OFF &&
        status != WIFI_HAL_WRONG_PASSWORD && status != WIFI_HAL_NO_AP_FOUND) {
        return WIFI_HAL_CONNECTING;
    }
    return status;
}

bool wifi_service_scanning()
{
    return s_scanning;
}

int32_t wifi_service_rssi()
{
    return wifi_hal_rssi();
}

int wifi_service_bars()
{
    if (wifi_hal_status() != WIFI_HAL_CONNECTED) {
        return 0;
    }
    const int32_t rssi = wifi_hal_rssi();
    if (rssi >= APP_WIFI_RSSI_EXCELLENT) return 4;
    if (rssi >= APP_WIFI_RSSI_GOOD)      return 3;
    if (rssi >= APP_WIFI_RSSI_FAIR)      return 2;
    return 1;
}

const char *wifi_service_ssid()
{
    wifi_hal_ssid(s_ssid_buf, sizeof(s_ssid_buf));
    return s_ssid_buf;
}

const char *wifi_service_ip()
{
    wifi_hal_ip(s_ip_buf, sizeof(s_ip_buf));
    return s_ip_buf;
}

const char *wifi_service_status_text()
{
    if (s_scanning) {
        return "Scanning...";
    }
    switch (wifi_service_status()) {
    case WIFI_HAL_OFF:             return "Off";
    case WIFI_HAL_CONNECTING:      return "Connecting...";
    case WIFI_HAL_CONNECTED:       return "Connected";
    case WIFI_HAL_NO_AP_FOUND:     return "Network not found";
    case WIFI_HAL_WRONG_PASSWORD:  return "Wrong password";
    case WIFI_HAL_CONNECT_FAILED:  return "Connection failed";
    case WIFI_HAL_IDLE:
    default:                       return s_saved_ssid[0] ? "Not connected" : "No saved network";
    }
}

// -- scanning --------------------------------------------------------------

void wifi_service_scan()
{
    if (s_scanning || !wifi_hal_radio_on()) {
        return;
    }
    wifi_hal_scan_start();
    s_scanning = true;
    notifyListeners();
}

int wifi_service_scan_count()
{
    return s_scan_count;
}

const WifiScanEntry *wifi_service_scan_entry(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return nullptr;
    }
    return &s_scan[index];
}

// -- association -----------------------------------------------------------

void wifi_service_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0]) {
        return;
    }
    strncpy(s_saved_ssid, ssid, sizeof(s_saved_ssid) - 1);
    s_saved_ssid[sizeof(s_saved_ssid) - 1] = '\0';
    strncpy(s_saved_password, password ? password : "", sizeof(s_saved_password) - 1);
    s_saved_password[sizeof(s_saved_password) - 1] = '\0';
    credentialsSave();

    // A new passphrase clears the latch, whether or not this one is right --
    // otherwise a single typo would mute auto-connect until the next reboot.
    s_password_rejected = false;

    wifi_hal_connect(s_saved_ssid, s_saved_password);
    s_connecting = true;
    s_connect_started_ms = lv_tick_get();
    s_retry_pending = false;
    notifyListeners();
}

bool wifi_service_has_saved()
{
    return s_saved_ssid[0] != '\0';
}

const char *wifi_service_saved_ssid()
{
    return s_saved_ssid;
}

void wifi_service_forget()
{
    wifi_hal_disconnect();
    s_saved_ssid[0] = '\0';
    s_saved_password[0] = '\0';
    credentialsErase();

    s_connecting = false;
    s_retry_pending = false;
    s_password_rejected = false;
    notifyListeners();
}

// -- listeners -------------------------------------------------------------

void wifi_service_add_listener(WifiListener cb)
{
    if (!cb) {
        return;
    }
    for (int i = 0; i < MAX_LISTENERS; ++i) {
        if (s_listeners[i] == cb) {
            return;         // already registered
        }
        if (!s_listeners[i]) {
            s_listeners[i] = cb;
            return;
        }
    }
    // Full. Silently dropping would make the missing refresh someone else's
    // debugging session, so say so once, at build-time cost of nothing.
    LV_LOG_WARN("wifi_service: listener table full, refresh will be missed");
}

// -- lifecycle -------------------------------------------------------------

void wifi_service_begin()
{
    credentialsLoad();

    // app_settings_begin() has already run, so this is the state the user left
    // the radio in. Going through the hal directly rather than through
    // wifi_service_set_enabled() keeps this from writing the setting straight
    // back and marking the store dirty on every boot.
    if (app_settings().wifi_enabled) {
        wifi_hal_radio(true);
        startSavedConnect();
    }
    s_last_status = wifi_hal_status();
}

void wifi_service_poll()
{
    // Scan completion. The hal's "running" flag is the authority; s_scanning is
    // this module's copy, held so the UI has something to show between the
    // request and the radio actually starting.
    if (s_scanning && !wifi_hal_scan_running()) {
        s_scan_count = wifi_hal_scan_collect(s_scan, APP_WIFI_MAX_SCAN_RESULTS);
        s_scanning = false;
        notifyListeners();
    }

    const WifiHalStatus status = wifi_hal_status();

    if (s_connecting) {
        if (status == WIFI_HAL_CONNECTED) {
            s_connecting = false;
            s_retry_pending = false;
        } else if (status == WIFI_HAL_WRONG_PASSWORD) {
            s_password_rejected = true;
            scheduleRetry();
        } else if (lv_tick_elaps(s_connect_started_ms) >= APP_WIFI_CONNECT_TIMEOUT_MS) {
            // Arduino reports WL_DISCONNECTED both while trying and after
            // giving up, so without this a failed attempt would sit in
            // "Connecting..." forever and never schedule a retry.
            wifi_hal_disconnect();
            scheduleRetry();
        }
    } else if (s_retry_pending && status != WIFI_HAL_CONNECTED && !s_scanning &&
               lv_tick_elaps(s_retry_from_ms) >= APP_WIFI_RECONNECT_MS) {
        startSavedConnect();
    } else if (status != WIFI_HAL_CONNECTED && s_last_status == WIFI_HAL_CONNECTED) {
        // Dropped an association we had. Auto-reconnect is off in the hal, so
        // getting back on is this module's job.
        scheduleRetry();
    }

    if (status != s_last_status) {
        s_last_status = status;
        notifyListeners();
    }
}
