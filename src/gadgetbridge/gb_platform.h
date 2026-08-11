/**
 * @file      gb_platform.h
 * @license   MIT
 * @brief     The handful of board services the protocol app needs.
 *
 * Everything the app does with hardware -- read the clock, set the clock, read
 * the battery, buzz the motor -- goes through here, so the differences between
 * the three LilyGo boards (and between hardware and the native emulator build)
 * are confined to gb_platform.cpp instead of being smeared through the app and
 * UI code.
 */
#pragma once

#include <stdint.h>
#include <time.h>

/**
 * Firmware revision, reported both in the `ver` message (§6.1) and through the
 * Device Information service. Override with -D GB_FW_VERSION=\"1.2.3\".
 */
#ifndef GB_FW_VERSION
#define GB_FW_VERSION "0.1.0"
#endif

/// DRV2605 waveform ids used for the two things this app buzzes about.
enum GbHaptic {
    GB_HAPTIC_TAP = 1,      ///< sharp single click -- new notification
    GB_HAPTIC_ALERT = 47,   ///< long buzz -- incoming call, alarm, "find device"
};

namespace gb_platform
{

/// Seed the system clock from the RTC. Call once, after instance.begin().
void begin();

/// The board name reported as the hardware revision (§6.1).
const char *hardwareName();

/**
 * Set the watch to @p epoch_local, a Unix timestamp already shifted into local
 * time (protocol §5.2 sends UTC plus an offset; the app does the arithmetic).
 *
 * Both the system clock and the RTC chip are written, so the time survives a
 * reboot and a sleep cycle.
 */
void setLocalTime(int64_t epoch_local);

/// Read the current local time. Returns false if the clock was never set.
bool localTime(struct tm &out);

/// Milliseconds since boot, for the app's own timers. Wraps like millis().
uint32_t uptimeMs();

/// True once the phone (or the RTC) has given us a real time.
bool timeIsValid();

/// Battery charge in percent, or -1 if the board cannot measure it.
int batteryPercent();

/// Battery voltage in volts, or 0 if unavailable.
float batteryVolts();

/// True while the charger is running.
bool charging();

/// Fire one haptic pulse. Non-blocking.
void vibrate(GbHaptic effect);

} // namespace gb_platform
