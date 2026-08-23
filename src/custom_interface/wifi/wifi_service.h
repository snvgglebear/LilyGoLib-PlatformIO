#pragma once

/**
 * @file      wifi_service.h
 * @license   MIT
 * @brief     Wi-Fi state, saved credentials and auto-connect -- everything the
 *            UI needs to show or change about the radio, in one place.
 *
 * The layer src/factory does not have. There, ui_wireless.cpp drives the radio
 * directly: it scans, it joins, and when the app closes the state it built up
 * goes with it. That works for a diagnostic app you open on purpose and closes
 * when you leave. Here Wi-Fi is a property of the watch -- the status bar and
 * the quick-settings tray show it while the settings page that changed it is
 * long gone -- so the state has to outlive any one screen.
 *
 * So this module owns:
 *
 *   - the radio's on/off state, mirrored into AppSettings so it survives a
 *     reboot;
 *   - the credentials, in their own NVS namespace (deliberately not in the
 *     AppSettings blob: that struct is echoed to Gadgetbridge field by field
 *     under §6.8, and a passphrase has no business in something that gets
 *     serialised to a phone);
 *   - auto-connect and its retry policy;
 *   - the scan results, as a static array with a stable index the UI can hold
 *     across a rebuild.
 *
 * Everything here runs on the LVGL/loop() task. wifi_service_poll() drives the
 * state machine and must be called from loopGui().
 */

#include "wifi_hal.h"

/// Radio power. Setting it applies immediately, persists through AppSettings,
/// and (when turning on with a saved network) starts an auto-connect.
bool wifi_service_enabled();
void wifi_service_set_enabled(bool enable);

/*State, for anything drawing an indicator.*/
WifiHalStatus wifi_service_status();
bool wifi_service_scanning();

/// Signal quality as 0-4 bars: 0 when not associated, 4 at
/// APP_WIFI_RSSI_EXCELLENT or better.
int wifi_service_bars();
int32_t wifi_service_rssi();

/// SSID of the current association, or "" when not associated.
const char *wifi_service_ssid();
/// Assigned IPv4 address, or "" when not associated.
const char *wifi_service_ip();

/// A short human-readable state, for a status row: "Off", "Connected",
/// "Wrong password", ... Never NULL.
const char *wifi_service_status_text();

/*Scanning. Asynchronous: start one, watch wifi_service_scanning(), read the
  results when it clears. Starting a scan while one is running is a no-op.*/
void wifi_service_scan();
int wifi_service_scan_count();
/// @p index must be < wifi_service_scan_count(). Never NULL for a valid index.
const WifiScanEntry *wifi_service_scan_entry(int index);

/**
 * Join @p ssid. The credentials are saved *before* the attempt, not after it
 * succeeds -- a watch that reboots mid-connect should come back and retry the
 * network the user chose, and a passphrase that turns out to be wrong is
 * corrected by entering it again, which overwrites this anyway.
 */
void wifi_service_connect(const char *ssid, const char *password);

/*The saved network, which is not necessarily the connected one.*/
bool wifi_service_has_saved();
const char *wifi_service_saved_ssid();

/// Drop the association and erase the stored credentials.
void wifi_service_forget();

/**
 * Called whenever the state a UI would draw has changed -- connected,
 * disconnected, scan finished, radio toggled.
 *
 * Several at once, because the status bar, the tray and the settings page all
 * want it and none of them owns the others. Registering the same callback
 * twice is ignored, so a screen rebuilt on every visit can register
 * unconditionally.
 */
typedef void (*WifiListener)(void);
void wifi_service_add_listener(WifiListener cb);

/// Load credentials, apply the saved radio state, and start any auto-connect.
/// Call once from setupGui(), after app_settings_begin() and wifi_hal_begin().
void wifi_service_begin();

/// Drive the state machine: connect timeouts, scan completion, reconnects.
/// Call from loopGui().
void wifi_service_poll();
