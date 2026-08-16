#include "quick_settings_tray_hal.h"

#include <stdio.h>

#ifdef ARDUINO

#include <LilyGoLib.h>

void qst_hal_get_time_date(QstTimeDate *out)
{
    RTC_DateTime now = instance.rtc.getDateTime();

    snprintf(out->time, sizeof(out->time), "%02u:%02u", now.getHour(), now.getMinute());

    // getWeek() is the weekday (0-6); getDay() is the day of month -- see
    // SensorRTC.h's own RTC_DateTime::printDatetime() for the same pairing.
    static const char *const weekday_name[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    uint8_t week = now.getWeek();
    snprintf(out->date, sizeof(out->date), "%s %02u/%02u",
             weekday_name[week > 6 ? 0 : week], now.getMonth(), now.getDay());
}

void qst_hal_get_battery(QstBattery *out)
{
    int percent = instance.pmu.getBatteryPercent();
    out->percent = percent < 0 ? 0 : percent;
    out->charging = instance.pmu.isCharging();
}

int qst_hal_get_brightness(void)
{
    return instance.getBrightness();
}

void qst_hal_set_brightness(int level)
{
    instance.setBrightness((uint8_t)level);
}

int qst_hal_brightness_min(void)
{
    return DEVICE_MIN_BRIGHTNESS_LEVEL;
}

int qst_hal_brightness_max(void)
{
    return DEVICE_MAX_BRIGHTNESS_LEVEL;
}

#else // !ARDUINO -- native/SDL2 emulator build

#include <stdlib.h>
#include <time.h>

void qst_hal_get_time_date(QstTimeDate *out)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    strftime(out->time, sizeof(out->time), "%H:%M", lt);
    strftime(out->date, sizeof(out->date), "%a %m/%d", lt);
}

void qst_hal_get_battery(QstBattery *out)
{
    // No PMU on the host -- fake a plausible reading. Same shape as
    // src/factory/hal_interface.cpp's native stub (30 + rand() % 71).
    out->percent = 30 + rand() % 71;
    out->charging = false;
}

static int s_native_brightness = 200;

int qst_hal_get_brightness(void)
{
    return s_native_brightness;
}

void qst_hal_set_brightness(int level)
{
    s_native_brightness = level;
}

int qst_hal_brightness_min(void)
{
    return 0;
}

int qst_hal_brightness_max(void)
{
    return 255;
}

#endif // ARDUINO
