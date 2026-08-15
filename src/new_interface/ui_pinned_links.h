/**
 * @file      ui_pinned_links.h
 * @license   MIT
 * @brief     Home screen row of pinned app shortcuts.
 */
#pragma once

#include <lvgl.h>

/// Build the pinned-links row in `parent`: one icon per app_registry() entry
/// whose PinnableApp bit is set in the pinned-apps mask (loaded from NVS on
/// the first build, default PINNED_APPS_DEFAULT_MASK, app_config.h), capped at
/// PINNED_APPS_MAX_VISIBLE, plus a trailing "All Apps" icon that opens
/// ui_all_apps_main. Clicking a pinned icon behaves exactly like the original
/// launcher row (both go through open_app(), ui_define.h). Safe to call again
/// on the same parent to rebuild (e.g. after a phone-driven settings update);
/// the previous row is discarded. Also registers the app_gadgetbridge.h
/// listener that applies GB_CHANGE_SETTINGS pinned_mask updates.
void ui_pinned_links_build(lv_obj_t *parent);
