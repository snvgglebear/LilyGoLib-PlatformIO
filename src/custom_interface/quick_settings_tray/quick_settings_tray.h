#pragma once

/**
 * @file      quick_settings_tray.h
 * @license   MIT
 * @brief     Swipe-down quick-settings shade: time/date, battery, brightness.
 *            See ../../.claude/swipe-down-quick-settings-tray-plan.md.
 *
 * Lives on lv_layer_top(), independent of whichever screen
 * (lv_screen_load()) is active, so one init() covers every screen the app
 * ever switches to -- open() just needs calling from the right place.
 *
 * The object tree is routed through usable_area.h's curved-bezel
 * engine, which lays out against the Ultra's geometry and degrades
 * to plain full-width containers on the flat-panel boards.
 */

#include <lvgl.h>

/// Build the scrim + tray on lv_layer_top(), hidden. Call once, after
/// usable_area_init() (needs usable_area_screen_width/height()).
void quick_settings_tray_init(void);

/// Slide the tray in over whatever screen is active and dim it. Call this
/// from a gesture handler on any non-watchface screen -- never from the
/// watchface's, which owns swipe-down for its own navigation.
void quick_settings_tray_open(void);

/// Slide the tray back out. A no-op when it is already closed or closing, so
/// callers do not need to check first.
void quick_settings_tray_close(void);

/// True while the tray is open or animating open. The tray sits on
/// lv_layer_top() and so survives an lv_screen_load(): anything that navigates
/// in response to a button needs this to decide whether the press means "put
/// the tray away" rather than "go somewhere else and leave it hanging there".
bool quick_settings_tray_is_open(void);

/**
 * Wire up the footer's gear button.
 *
 * A callback rather than the tray calling settings_screen_open() itself: this
 * module knows about the clock, the battery and the backlight and nothing else
 * in the app, and a direct call would make it depend on the settings module for
 * one button. setupGui() connects the two. With no action set the gear is
 * hidden, so the tray still stands alone.
 *
 * The tray closes itself before @p cb runs -- it lives on lv_layer_top() and
 * would otherwise hang over whatever screen the action loads.
 */
typedef void (*QstAction)(void);
void quick_settings_tray_set_action(QstAction cb);
