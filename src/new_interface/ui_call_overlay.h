/**
 * @file      ui_call_overlay.h
 * @license   MIT
 * @brief     Phone-call overlay: answer/reject/end, driven by GbApp call state.
 */
#pragma once

/// Register with app_gb_add_listener() so the overlay raises/updates/dismisses
/// itself on GB_CHANGE_CALL. Call once, after app_gb_init(). Safe to call even
/// if no call is in progress -- the overlay only appears once one starts.
void ui_call_overlay_init(void);
