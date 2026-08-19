#pragma once

/**
 * @file      face_registry.h
 * @license   MIT
 * @brief     The set of installable watch faces, and the one that is showing.
 *
 * The faces themselves (simple_face, batman_dial) know how to build and tear
 * themselves down; this decides which one is up. Before it existed, the choice
 * was a commented-out line in setupGui() -- see app_setup.cpp -- so it was a
 * recompile rather than a setting.
 *
 * Adding a face is three edits, all in face_registry.cpp: a WatchFaceId, a
 * display name, and its init/deinit pair in the table.
 */

#include <lvgl.h>
#include <stdint.h>

enum WatchFaceId : uint8_t {
    WATCH_FACE_DIGITAL = 0,   ///< simple_face -- time, date, battery bar
    WATCH_FACE_ANALOG  = 1,   ///< batman_dial -- drawn hands, battery arc
    WATCH_FACE_COUNT,
};

/// Human-readable name, for the settings page. Never NULL.
const char *watch_face_name(WatchFaceId id);

/**
 * Adopt @p screen as the home screen and build the face the settings store
 * currently names. Call once from setupGui(), after app_settings_begin() and
 * after usable_area_init() has styled @p screen.
 */
void watch_face_begin(lv_obj_t *screen);

/**
 * Tear down the showing face and build @p id in its place.
 *
 * A no-op before watch_face_begin() (there is no screen to build on yet) and
 * a no-op if @p id is already showing, so app_settings_begin() can call it
 * unconditionally during boot and again on every later change.
 */
void watch_face_apply(WatchFaceId id);

/// Which face is up, or the stored choice if none has been built yet.
WatchFaceId watch_face_current(void);
