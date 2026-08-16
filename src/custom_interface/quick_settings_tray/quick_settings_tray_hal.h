#pragma once

/**
 * @file      quick_settings_tray_hal.h
 * @license   MIT
 * @brief     Tiny hardware shim for the quick-settings tray: clock, battery,
 *            brightness. custom_interface has no hal_interface.h-style
 *            wrapper layer (see ../../.claude/swipe-down-quick-settings-tray-plan.md,
 *            "Current state"), so this covers only the handful of calls the
 *            tray itself needs, split ARDUINO / native by hand like
 *            ../screen_state/screen_state.cpp does.
 */

struct QstTimeDate {
    char time[6];   ///< "HH:MM"
    char date[16];  ///< "Sat 08/16"
};

/// instance.rtc.getDateTime() on hardware; the host's wall clock natively.
void qst_hal_get_time_date(QstTimeDate *out);

struct QstBattery {
    int percent;    ///< 0-100
    bool charging;
};

/// instance.pmu.getBatteryPercent()/isCharging() on hardware; a fixed/jittered
/// fake reading natively -- there's no PMU on the host.
void qst_hal_get_battery(QstBattery *out);

/// instance.getBrightness()/setBrightness(), DEVICE_MIN/MAX_BRIGHTNESS_LEVEL
/// (0-255 on T-Watch-Ultra) on hardware; a plain static standing in for the
/// backlight natively.
int qst_hal_get_brightness(void);
void qst_hal_set_brightness(int level);
int qst_hal_brightness_min(void);
int qst_hal_brightness_max(void);
