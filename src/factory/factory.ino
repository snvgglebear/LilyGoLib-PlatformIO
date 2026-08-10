/**
 * @file      factory.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 * @brief     Arduino entry point for the factory demo firmware.
 *
 * This is one of two possible entry points for the application; which one is
 * compiled depends on whether the `ARDUINO` macro is defined:
 *
 *   - `ARDUINO` defined   -> this file. Built for real ESP32-S3 hardware
 *                            (`twatchs3`, `twatch_ultra`, `tlora_pager` envs)
 *                            against the Arduino-ESP32 framework.
 *   - `ARDUINO` undefined -> `main.cpp` in this directory. Built for the
 *                            native/SDL2 desktop emulator (`emulator_*` envs).
 *
 * Both paths converge on the same two calls -- `hw_init()` (peripheral bring-up,
 * see hal_interface.cpp) and `setupGui()` (LVGL UI bring-up, see ui_main.cpp) --
 * so everything above the HAL runs unmodified on hardware and in the emulator.
 * The whole file is wrapped in `#ifdef ARDUINO` so the native build simply
 * compiles it away to nothing.
 *
 * Responsibilities unique to this file:
 *   - Bring up the serial console, WiFi, and SNTP time sync.
 *   - Create the mutex that serialises access to the shared `instance` object.
 *   - Drive the Arduino `loop()`: service the board library, the NFC reader,
 *     and the LVGL timer handler.
 *
 * @see LilyGoLib board support library: https://github.com/Xinyuan-LilyGO/LilyGoLib
 * @see Arduino-ESP32 core docs:         https://docs.espressif.com/projects/arduino-esp32/en/latest/
 */
#ifdef ARDUINO
#include <LilyGoLib.h>      // Board support: declares the global `instance` object
#include <LV_Helper.h>      // LilyGoLib's LVGL glue (display flush + input device wiring)
#include <WiFi.h>
#include <esp_sntp.h>       // ESP-IDF SNTP client, used to set the clock from the network
#include "hal_interface.h"
#include <WiFi.h>
#include "event_define.h"

// Defined in ui_main.cpp -- builds the launcher screen and all app screens.
extern void setupGui();

// NTP pools queried once WiFi obtains an IP. Two servers are configured so a
// single unreachable pool does not block the time sync.
static const char *ntpServer1 = "pool.ntp.org";
static const char *ntpServer2 = "time.nist.gov";
// Timezone offset in seconds east of UTC. GMT_OFFSET_SECOND is supplied by the
// build (see platformio.ini build_flags) so the firmware can be retargeted to a
// different timezone without touching this file.
static const uint64_t  gmtOffset_sec = GMT_OFFSET_SECOND;
// Extra DST offset. Left at 0: the demo does not implement DST rules.
static const int   daylightOffset_sec = 0;

// Guards the global `instance` object. The LVGL/UI code runs from loop(), but
// WiFi and other ESP-IDF subsystems raise callbacks on their own FreeRTOS tasks,
// so any code touching `instance` off the main task must bracket it with
// instanceLockTake()/instanceLockGive().
// @see https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html
static SemaphoreHandle_t xSemaphore = NULL;


/**
 * Acquire the global `instance` mutex, blocking indefinitely (portMAX_DELAY).
 * A failure here is unrecoverable -- it means the scheduler is in a bad state --
 * so the code asserts rather than continuing with unsynchronised access.
 */
void instanceLockTake()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) != pdTRUE) {
            log_e("Failed to take semaphore");
            assert(0);
        }
    }
}

/**
 * Release the global `instance` mutex. Must be paired with instanceLockTake()
 * on the same task -- FreeRTOS mutexes are owned by the taking task.
 */
void instanceLockGive()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreGive(xSemaphore) != pdTRUE) {
            log_e("Failed to give semaphore");
            assert(0);
        }
    }
}

/**
 * SNTP "time synchronised" notification, registered below via
 * sntp_set_time_sync_notification_cb(). Called once the system clock has been
 * stepped/smoothed to the network time.
 *
 * The ESP32's system clock is volatile, so the freshly corrected time is pushed
 * down into the battery-backed external RTC (PCF85063 / an on-board equivalent,
 * depending on the board). getDeviceProbe() returns a bitmask of peripherals
 * that answered during instance.begin(); HW_RTC_ONLINE means the RTC is present,
 * so the write is skipped on boards where it is absent or failed to probe.
 *
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/system_time.html
 */
static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    // printLocalTime();
    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        instance.rtc.hwClockWrite();     // copy system time -> hardware RTC
    }
}

/**
 * WiFi STA "got IP" event handler.
 *
 * WARNING: This function is called from a separate FreeRTOS task (thread)!
 * Keep it short and do not touch LVGL objects or `instance` from here without
 * taking the mutex -- LVGL is not thread-safe.
 *
 * Starting SNTP is deferred to this point because configTime() immediately
 * begins DNS resolution, which requires a usable network interface.
 *
 * @see https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
 */
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    // Kick off SNTP; completion is reported asynchronously via time_available().
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

/**
 * Arduino one-shot initialisation. Ordering matters here:
 * clocks/serial -> synchronisation primitives -> network -> board -> LVGL -> app.
 */
void setup()
{
    // Run the S3 at its full 240 MHz. LVGL rendering and the audio/FFT paths are
    // CPU-bound, so the demo does not trade clock speed for power here.
    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    // Create the `instance` mutex *before* anything can schedule a callback that
    // might try to take it.
    xSemaphore = xSemaphoreCreateMutex();
    if (xSemaphore == NULL) {
        log_e("Failed to create mutex");
        assert(0);
    }

    sntp_set_time_sync_notification_cb(time_available);

    // Examples of different ways to register wifi events;
    // these handlers will be called from another thread.
    WiFi.mode(WIFI_STA);                 // station mode: join an AP, do not create one
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // Auto-reconnect is off and the radio starts disconnected: the WiFi app in
    // ui_wireless.cpp owns scanning/joining, so the firmware does not silently
    // re-associate with a stored AP behind the UI's back.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);               // true = also erase the stored credentials

    // Probe and initialise every on-board peripheral. The commented-out flags
    // show how to skip individual subsystems (useful when a module is unpopulated
    // or when shortening boot time during development).
    instance.begin(/*NO_HW_LORA|NO_HW_RTC|NO_HW_GPS|NO_HW_LORA*/);

    // Wire LVGL to this board: allocate draw buffers, register the display flush
    // callback, and register touch/encoder/keyboard input devices.
    beginLvglHelper(instance);

    hw_init();      // app-level peripheral setup (radio, sensors, audio) -- hal_interface.cpp

    setupGui();     // build the launcher and register every app -- ui_main.cpp

    Serial.println("Start done. run main loop");
}

// Defined in app_nfc.cpp; only linked in when the ST25R3916 NFC reader is present.
extern void loopNFCReader();

/**
 * Main superloop. Everything inside the lock runs single-threaded with respect
 * to other users of `instance`.
 *
 * Order of work per iteration:
 *   1. instance.loop()      -- board library housekeeping (PMU/IRQ/GPS feed, etc.)
 *   2. loopNFCReader()      -- poll the NFC front end for tags (Ultra / Pager only)
 *   3. lv_timer_handler()   -- run LVGL timers, animations, input reading, redraw
 *
 * The trailing delay(5) yields to lower-priority FreeRTOS tasks and the idle
 * task (which feeds the watchdog); it also roughly paces the UI to ~200 Hz max.
 *
 * @see https://docs.lvgl.io/master/details/main-components/timer.html
 */
void loop()
{
    instanceLockTake();
    instance.loop();
#if defined(USING_ST25R3916)
    loopNFCReader();
#endif
    lv_timer_handler();
    instanceLockGive();
    delay(5);
}

#endif
