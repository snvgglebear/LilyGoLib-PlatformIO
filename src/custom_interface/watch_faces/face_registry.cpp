/**
 * @file      face_registry.cpp
 * @license   MIT
 * @brief     Which watch face is showing. See face_registry.h.
 */
#include "face_registry.h"

#include "batman_dial.h"
#include "simple_face.h"
#include "../app_config.h"
#include "../settings/app_settings.h"

namespace
{

struct FaceEntry {
    const char *name;
    void (*init)(lv_obj_t *screen);
    void (*deinit)(void);
};

const FaceEntry FACES[WATCH_FACE_COUNT] = {
    {"Digital", simple_face_init, simple_face_deinit},
    {"Analog",  batman_dial_init, batman_dial_deinit},
};

lv_obj_t *s_screen = nullptr;          ///< NULL until watch_face_begin()
WatchFaceId s_current = WATCH_FACE_COUNT;   ///< COUNT = nothing built yet

bool valid(WatchFaceId id)
{
    return id < WATCH_FACE_COUNT;
}

} // namespace

const char *watch_face_name(WatchFaceId id)
{
    return valid(id) ? FACES[id].name : "?";
}

WatchFaceId watch_face_current(void)
{
    return s_current;
}

void watch_face_apply(WatchFaceId id)
{
    if (!s_screen || !valid(id) || id == s_current) {
        return;
    }
    if (valid(s_current)) {
        FACES[s_current].deinit();
    }
    FACES[id].init(s_screen);
    s_current = id;
}

void watch_face_begin(lv_obj_t *screen)
{
    s_screen = screen;
    // Deliberately read through app_settings() rather than taking the id as an
    // argument: the store is the one place that knows which face was chosen,
    // and this way setupGui() cannot pass a stale value.
    //
    // Clamped, not just validated: watch_face_apply() ignores an out-of-range
    // id, which at boot would leave the home screen empty rather than merely
    // showing the wrong face.
    WatchFaceId id = (WatchFaceId)app_settings().watch_face;
    watch_face_apply(valid(id) ? id : (WatchFaceId)APP_WATCH_FACE_DEFAULT);
}
