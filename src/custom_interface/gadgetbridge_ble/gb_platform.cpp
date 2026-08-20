/**
 * @file      gb_platform.cpp
 * @license   MIT
 * @brief     Board services for the Gadgetbridge app -- and their host stubs.
 *
 * The Arduino half talks to LilyGoLib's global `instance`; the native half
 * fakes just enough (host clock, a made-up battery) that the emulator build can
 * run the same app and UI code.
 */
#include "gb_platform.h"

#include <sys/time.h>

#ifdef ARDUINO

#include <LilyGoLib.h>

namespace
{

/// Anything before 2020 means "never set" -- the RTC powers up around 2000.
constexpr int64_t GB_MIN_VALID_EPOCH = 1577836800;   // 2020-01-01T00:00:00Z

bool rtcOnline()
{
    return (instance.getDeviceProbe() & HW_RTC_ONLINE) != 0;
}

} // namespace

void gb_platform::begin()
{
    // The system clock is what the rest of the app reads, so start it from the
    // RTC. hwClockRead() does the settimeofday() for us.
    if (rtcOnline()) {
        instance.rtc.hwClockRead();
    }
}

const char *gb_platform::hardwareName()
{
#if defined(ARDUINO_T_LORA_PAGER)
    return "T-LoRa Pager";
#else
    // ARDUINO_T_WATCH_S3_ULTRA, and the native build, which stands in for it.
    return "T-Watch Ultra";
#endif
}

void gb_platform::setLocalTime(int64_t epoch_local)
{
    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(epoch_local);
    settimeofday(&tv, nullptr);

    if (rtcOnline()) {
        // The system clock holds local time and TZ is left at UTC, so gmtime_r
        // is what turns it back into the wall-clock fields the RTC wants.
        time_t seconds = static_cast<time_t>(epoch_local);
        struct tm local = {};
        gmtime_r(&seconds, &local);
        instance.rtc.setDateTime(RTC_DateTime(local));
    }
}

bool gb_platform::localTime(struct tm &out)
{
    time_t now = time(nullptr);
    if (now < GB_MIN_VALID_EPOCH) {
        return false;
    }
    gmtime_r(&now, &out);
    return true;
}

bool gb_platform::timeIsValid()
{
    return time(nullptr) >= GB_MIN_VALID_EPOCH;
}

uint32_t gb_platform::uptimeMs()
{
    return millis();
}

int gb_platform::batteryPercent()
{
#if defined(USING_BQ_GAUGE)
    if (instance.getDeviceProbe() & HW_GAUGE_ONLINE) {
        instance.gauge.refresh();
        return instance.gauge.getStateOfCharge();
    }
    return -1;
#elif defined(USING_PMU_MANAGE)
    return instance.pmu.getBatteryPercent();
#else
    return -1;
#endif
}

float gb_platform::batteryVolts()
{
#if defined(USING_BQ_GAUGE)
    if (instance.getDeviceProbe() & HW_GAUGE_ONLINE) {
        return instance.gauge.getVoltage() / 1000.0f;
    }
    return 0.0f;
#elif defined(USING_PMU_MANAGE)
    return instance.pmu.getBattVoltage() / 1000.0f;
#else
    return 0.0f;
#endif
}

bool gb_platform::charging()
{
#if defined(USING_PPM_MANAGE)
    return instance.ppm.isCharging();
#elif defined(USING_PMU_MANAGE)
    return instance.pmu.isCharging();
#else
    return false;
#endif
}

void gb_platform::vibrate(GbHaptic effect)
{
    uint8_t previous = instance.getHapticEffects();
    instance.setHapticEffects(static_cast<uint8_t>(effect));
    instance.vibrator();
    instance.setHapticEffects(previous);

    // The DRV2605 haptic driver and the touch controller share one I2C bus,
    // and LilyGoLib has no mutex over it (only over SPI) -- a haptic write can
    // leave the touch controller's IRQ/I2C state wedged so it stops reporting
    // touches. wakeupTouch() re-pulses the touch controller's reset line and
    // re-arms its interrupt, which also recovers it from this.
    instance.wakeupTouch();
}

#else // !ARDUINO -- native/SDL2 emulator build

#include <stdio.h>

namespace
{
bool s_time_set = false;
}

void gb_platform::begin()
{
    // The host clock is already running; treat it as the RTC.
    s_time_set = true;
}

const char *gb_platform::hardwareName()
{
    return "T-Watch Ultra";
}

void gb_platform::setLocalTime(int64_t epoch_local)
{
    // Do not stomp on the host's clock -- just report what a real watch would do.
    time_t seconds = static_cast<time_t>(epoch_local);
    struct tm local = {};
    gmtime_r(&seconds, &local);
    printf("[gb] time set to %04d-%02d-%02d %02d:%02d:%02d (local)\n",
           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
           local.tm_hour, local.tm_min, local.tm_sec);
    s_time_set = true;
}

bool gb_platform::localTime(struct tm &out)
{
    time_t now = time(nullptr);
    localtime_r(&now, &out);
    return true;
}

bool gb_platform::timeIsValid()
{
    return s_time_set;
}

uint32_t gb_platform::uptimeMs()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

int gb_platform::batteryPercent()
{
    return 76;
}

float gb_platform::batteryVolts()
{
    return 3.92f;
}

bool gb_platform::charging()
{
    return false;
}

void gb_platform::vibrate(GbHaptic effect)
{
    printf("[gb] vibrate (effect %d)\n", static_cast<int>(effect));
}

#endif // ARDUINO
