#pragma once

/**
 * @file      wifi_page.h
 * @license   MIT
 * @brief     The Wi-Fi subpage of the settings menu.
 *
 * Its own file rather than another build*Page() in settings_screen.cpp: this
 * one page is a scan list, a modal password prompt and an on-screen keyboard
 * with a lifetime of their own, which is more than the rest of that file's
 * pages put together.
 *
 * What it replaces is src/factory/ui_wireless.cpp, a standalone app with an
 * SSID dropdown, a password box and a connect button. The parts that do not
 * come across:
 *
 *   - the dropdown, for the reason settings_widgets.h gives for having no
 *     create_dropdown(): an open lv_dropdown list is a floating box outside
 *     the usable-area engine and can render under the Ultra's bezel. A
 *     scrollable lv_list inside the page cannot.
 *   - the 20-second animated progress bar. Factory animates a bar to 100% and
 *     treats arrival as completion, which is a timer wearing a progress bar's
 *     clothes. The status row here reports what wifi_service actually says.
 *   - WIFI_SSID / WIFI_PASSWORD compile-time credentials. Those exist so a
 *     factory-test rig can join a bench AP without typing; this app saves what
 *     the user entered instead.
 */

#include <lvgl.h>

/// Build the page's rows into @p page (an lv_menu page).
void settings_wifi_page_build(lv_obj_t *page);

/// Drop the refresh timer and any open password prompt, and forget the widget
/// pointers. Call before the menu is rebuilt and when the screen unloads --
/// the same contract settings_screen.cpp's stopInfoTimer() has.
void settings_wifi_page_stop(void);
