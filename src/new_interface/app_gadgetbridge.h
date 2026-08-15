/**
 * @file      app_gadgetbridge.h
 * @license   MIT
 * @brief     The one seam between GbApp (gadgetbridge_ble/) and this app's UI.
 *
 * GbApp::begin() takes exactly one listener. Several independent UI modules
 * (the notification toast, the alarms screen, a call overlay) all need to
 * react to GbApp state changes without knowing about each other -- that's
 * what src/custom_interface/plan.md means by "decouple the gadgetbridge
 * functionality from the ui setup, so it can be tested independently and
 * reused". app_gadgetbridge.cpp is that decoupling point: it owns the single
 * listener GbApp calls, and fans it out to whichever modules registered.
 *
 * gadgetbridge_ble/ itself stays exactly as ported from src/custom_interface
 * -- nothing in that directory needs to know this file exists.
 */
#pragma once

#include "gadgetbridge_ble/gb_app.h"

/// Bring up the transport + protocol state machine. Call once, after
/// usable_area_init(), before any UI module that calls app_gb_add_listener()
/// is built (setupGui() satisfies this ordering).
void app_gb_init();

/// Drain the link and run GbApp's periodic housekeeping (battery report,
/// alarm/buzzer polling). Call every loop() iteration, same as gb_app.poll()
/// would be called directly.
void app_gb_poll();

typedef void (*gb_listener_t)(GbStateChange change);

/// Register to be called back whenever GbApp's state changes. Listeners are
/// called in registration order; there is no way to unregister, which is
/// fine here since every caller registers once at boot and lives forever.
/// @return false if the fixed-size listener table is full (see
///         app_gadgetbridge.cpp) -- treat as a programming error, not a
///         runtime condition to recover from.
bool app_gb_add_listener(gb_listener_t fn);
