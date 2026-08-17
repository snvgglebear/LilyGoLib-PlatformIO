/**
 * @file      new_interface.ino
 * @license   MIT
 * @brief     Arduino entry point for the new_interface firmware.
 *
 * Adapted from src/factory/factory.ino: same WiFi/NTP/mutex bring-up and the
 * same hw_init()/setupGui() pair, plus two additions ported in from
 * src/custom_interface: usable_area_init() (safe-area engine for the
 * T-Watch-Ultra's curved bezel -- see usable_area.h) and
 * app_gb_init()/app_gb_poll() (the gadgetbridge_ble link, via the
 * app_gadgetbridge.h seam described there).
 *
 * This is one of two possible entry points; main.cpp in this directory is
 * the native/SDL2 counterpart, built instead of this file when `ARDUINO` is
 * undefined (the emulator_* envs). Both converge on the same hw_init(),
 * usable_area_init(), app_gb_init() and setupGui() calls, so everything
 * above the HAL/link layer runs unmodified on hardware and in the emulator.
 *
 * Currently gated to ARDUINO_T_WATCH_S3_ULTRA, same as src/custom_interface
 * -- the safe-area math (usable_area.h) is calibrated against that board's
 * 410x502 curved panel and does not yet have S3/Pager equivalents.
 */
#ifdef ARDUINO
#if defined(ARDUINO_T_WATCH_S3_ULTRA)

#include <LilyGoLib.h>      // Board support: declares the global `instance` object
#include <LV_Helper.h>      // LilyGoLib's LVGL glue (display flush + input device wiring)
#include <WiFi.h>
#include <esp_sntp.h>       // ESP-IDF SNTP client, used to set the clock from the network
#include "hal_interface.h"
#include "event_define.h"
#include "app_config.h"
#include "app_gadgetbridge.h"
#include <usable_area.h>
#include "ui_boot_button.h"

// Defined in ui_main.cpp -- builds the launcher screen and all app screens.
extern void setupGui();

// NTP pools queried once WiFi obtains an IP. Two servers are configured so a
// single unreachable pool does not block the time sync.
static const char *ntpServer1 = "pool.ntp.org";
static const char *ntpServer2 = "time.nist.gov";
static const uint64_t  gmtOffset_sec = GMT_OFFSET_SECOND;
static const int   daylightOffset_sec = 0;

// Guards the global `instance` object -- see factory.ino's instanceLockTake()
// doc comment for the full rationale (WiFi/SNTP callbacks run on their own
// FreeRTOS task and must not touch `instance`/LVGL unsynchronised).
static SemaphoreHandle_t xSemaphore = NULL;

void instanceLockTake()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) != pdTRUE) {
            log_e("Failed to take semaphore");
            assert(0);
        }
    }
}

void instanceLockGive()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreGive(xSemaphore) != pdTRUE) {
            log_e("Failed to give semaphore");
            assert(0);
        }
    }
}

static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        instance.rtc.hwClockWrite();     // copy system time -> hardware RTC
    }
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

void setup()
{
    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    xSemaphore = xSemaphoreCreateMutex();
    if (xSemaphore == NULL) {
        log_e("Failed to create mutex");
        assert(0);
    }

    sntp_set_time_sync_notification_cb(time_available);

    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // Auto-reconnect is off and the radio starts disconnected: the WiFi app
    // owns scanning/joining, so the firmware does not silently re-associate
    // with a stored AP behind the UI's back.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

    instance.begin();

    beginLvglHelper(instance);   // wire LVGL to the display + touch
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);   // instance.begin() leaves backlight at 0

    usable_area_init();          // safe-area engine, before any UI is built

    ui_boot_button_init();       // GPIO0 as a runtime input (short/long press)

    hw_init();                   // app-level peripheral setup (radio, sensors, audio)

    app_gb_init();                // gadgetbridge_ble link, before setupGui() so the
                                  // home screen's listeners can register against it

    setupGui();                  // build the launcher and register every app

    Serial.println("Start done. run main loop");
}

// Defined in app_nfc.cpp; only linked in when the ST25R3916 NFC reader is present.
extern void loopNFCReader();

void loop()
{
    instanceLockTake();
    instance.loop();
#if defined(USING_ST25R3916)
    loopNFCReader();
#endif
    app_gb_poll();
    ui_boot_button_poll();
    lv_timer_handler();
    instanceLockGive();
    delay(5);
}

#endif // ARDUINO_T_WATCH_S3_ULTRA
#endif // ARDUINO
