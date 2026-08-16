/**
 * @file      gb_link.h
 * @license   MIT
 * @brief     Transport-facing API: how the app talks to whatever carries the
 *            protocol lines.
 *
 * Two implementations exist, chosen at compile time by the build:
 *
 *   - gb_ble.cpp         Nordic UART Service over BLE (§1), gated on
 *                        `ARDUINO`, the real thing.
 *   - gb_link_stdio.cpp  stdin/stdout, for the native/SDL2 emulator build
 *                        (`pio run -e emulator_watch_ultra -t exec`), ported
 *                        from src/gadgetbridge's version unchanged.
 *
 * Everything here is called from the Arduino loop() / native main loop
 * context only. The BLE implementation does its own hand-off from the
 * NimBLE host task.
 */
#pragma once

#include <stdint.h>

#include <string>

#include "gb_protocol.h"

/**
 * Bring the link up: start advertising (BLE) or open stdin (native).
 *
 * @param handler receives decoded messages, from gb_link_poll() only.
 */
void gb_link_begin(GbProtocolHandler &handler);

/**
 * Drain whatever arrived since the last call and dispatch it.
 *
 * Incoming lines are buffered by the transport as soon as they arrive, which is
 * what lets the firmware take its time here: §7 warns that Gadgetbridge queues
 * `ver` and `time` immediately after subscribing to the TX characteristic, so
 * they can land before the app is finished starting up.
 */
void gb_link_poll();

/// Send one message (§6). The trailing '\n' and any chunking are added here.
bool gb_link_send(const std::string &json);

/// True once a phone is connected and subscribed to the TX characteristic.
bool gb_link_connected();

/// The advertised device name, e.g. "T-Watch Ultra 4F2A".
const char *gb_link_device_name();

/// Publish the battery level on the standard Battery Service (§1). No-op natively.
void gb_link_set_battery_level(int percent);
