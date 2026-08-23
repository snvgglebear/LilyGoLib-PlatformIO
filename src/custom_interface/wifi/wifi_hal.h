#pragma once

/**
 * @file      wifi_hal.h
 * @license   MIT
 * @brief     The Wi-Fi calls the app needs, split ARDUINO / native by hand.
 *
 * A port of the hw_*_wifi() family in src/factory/hal_interface.cpp, narrowed
 * to what this app uses and reshaped the way quick_settings_tray_hal.h is:
 * custom_interface has no hal_interface.h-style wrapper layer, so each module
 * brings its own shim rather than sharing one 1700-line header.
 *
 * Two differences from factory's version, both about not needing <WiFi.h> in
 * every caller:
 *
 *   - fixed-size char buffers instead of std::string / std::vector, so this
 *     header pulls in nothing and the scan list can live in a static array
 *     rather than reallocating on a device with 320 KB of RAM;
 *   - WifiHalStatus instead of wl_status_t, so the UI switches on a value that
 *     exists in the native build too. factory backfills wl_status_t by hand in
 *     hal_interface.h for exactly this reason.
 *
 * Everything here is safe to call from the LVGL/loop() task and nothing else.
 * The one thing that runs off-task is the got-IP handler inside wifi_hal.cpp,
 * which starts SNTP and touches no state this header exposes.
 */

#include <stddef.h>
#include <stdint.h>

/// 32 octets is the 802.11 maximum, plus a NUL.
constexpr size_t WIFI_SSID_MAX = 33;
/// 63 characters is the WPA2 passphrase maximum, plus a NUL.
constexpr size_t WIFI_PASSWORD_MAX = 64;

/**
 * Connection state, flattened from Arduino's wl_status_t.
 *
 * WIFI_HAL_WRONG_PASSWORD is kept apart from WIFI_HAL_CONNECT_FAILED because
 * it is the one failure the user can act on, and the settings page says so
 * rather than making them guess -- the same distinction factory's
 * ui_msg.cpp progress bar draws.
 */
enum WifiHalStatus : uint8_t {
    WIFI_HAL_OFF,              ///< radio down
    WIFI_HAL_IDLE,             ///< radio up, not associated and not trying
    WIFI_HAL_CONNECTING,
    WIFI_HAL_CONNECTED,
    WIFI_HAL_NO_AP_FOUND,      ///< the SSID was not on the air
    WIFI_HAL_WRONG_PASSWORD,
    WIFI_HAL_CONNECT_FAILED,   ///< associated then dropped, or refused
};

struct WifiScanEntry {
    char ssid[WIFI_SSID_MAX];
    int32_t rssi;       ///< dBm, negative
    uint8_t authmode;   ///< wifi_auth_mode_t; 0 is OPEN -- see wifi_hal_entry_is_open()
    uint8_t channel;
};

/// True for a network that takes no passphrase.
bool wifi_hal_entry_is_open(const WifiScanEntry &entry);

/**
 * Put the radio in station mode and register the got-IP handler that starts
 * SNTP. Call once from setupGui(), before wifi_service_begin().
 *
 * Auto-reconnect and Arduino's own credential persistence are both turned off,
 * as in factory: wifi_service.cpp owns when to associate, and it keeps the
 * credentials itself. Leaving the ESP-IDF copy in NVS as well would mean two
 * stores that can disagree, and a "forget network" that only forgets one.
 */
void wifi_hal_begin();

/*Radio power. Turning it off drops any association.*/
void wifi_hal_radio(bool on);
bool wifi_hal_radio_on();

/*Scanning. Start is asynchronous: poll wifi_hal_scan_running() and collect
  once it goes false.*/
void wifi_hal_scan_start();
bool wifi_hal_scan_running();

/// Copy up to @p max_entries results into @p out, strongest first. Returns how
/// many were written, or 0 if the scan found nothing or never ran.
int wifi_hal_scan_collect(WifiScanEntry *out, int max_entries);

/*Association. connect() is asynchronous too -- watch wifi_hal_status().*/
void wifi_hal_connect(const char *ssid, const char *password);
void wifi_hal_disconnect();

bool wifi_hal_connected();
WifiHalStatus wifi_hal_status();

/// Signal strength in dBm, or 0 when not associated.
int32_t wifi_hal_rssi();

/*Both write a NUL-terminated string and both write "" when not associated.*/
void wifi_hal_ssid(char *out, size_t len);
void wifi_hal_ip(char *out, size_t len);
