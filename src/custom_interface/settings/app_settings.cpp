/**
 * @file      app_settings.cpp
 * @license   MIT
 * @brief     Load/store/apply for AppSettings. See app_settings.h.
 *
 * Storage is split ARDUINO / native by hand, the way screen_state.cpp and
 * quick_settings_tray_hal.cpp are: NVS through Preferences on the boards, a
 * plain file on the host so the emulator can exercise the same load/flush path
 * rather than starting from defaults every run.
 */
#include "app_settings.h"

#include "../app_config.h"
#include "../quick_settings_tray/quick_settings_tray_hal.h"
#include "../screen_state/screen_state.h"
#include "../watch_faces/face_registry.h"

#include <string.h>

// <Preferences.h> (and, transitively, half of the Arduino core) must be
// included here, at file scope, not down in the `#ifdef ARDUINO` block below:
// that block sits inside the anonymous namespace, and a header included while
// lexically inside an unnamed namespace gets any `namespace std { ... }` it
// opens nested inside that unnamed namespace too, as a distinct namespace from
// the real ::std. Arduino.h's `using std::abs;` then fails to resolve on the
// xtensa-esp32s3 toolchain, and every std:: symbol Preferences.h's own
// includes expect already exists follows it into the same wall of errors.
#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace
{

AppSettings s_settings;
bool s_dirty = false;

/// Brightness has no compile-time default: the level range is board dependent
/// (qst_hal_brightness_min()/max()), so the default is a percentage of it.
int defaultBrightness()
{
    const int lo = qst_hal_brightness_min();
    const int hi = qst_hal_brightness_max();
    return lo + (hi - lo) * APP_BRIGHTNESS_DEFAULT_PCT / 100;
}

void loadDefaults()
{
    s_settings.version          = APP_SETTINGS_VERSION;
    s_settings.brightness       = (uint8_t)defaultBrightness();
    s_settings.wrist_wake       = APP_WRIST_WAKE_DEFAULT;
    s_settings.screen_timeout_s = APP_SCREEN_TIMEOUT_DEFAULT_S;
    s_settings.watch_face       = APP_WATCH_FACE_DEFAULT;
    s_settings.vibrate_messages = APP_VIBRATE_MESSAGES_DEFAULT;
    s_settings.vibrate_alerts   = APP_VIBRATE_ALERTS_DEFAULT;
    s_settings.notif_popup_ms   = APP_NOTIF_POPUP_DEFAULT_MS;
    s_settings.pinned_mask      = APP_PINNED_MASK_DEFAULT;
    s_settings.low_batt_pct     = APP_LOW_BATT_DEFAULT_PCT;
    s_settings.lora_enabled     = APP_LORA_ENABLED_DEFAULT;
}

/**
 * Push every value into the subsystem that owns it.
 *
 * Not every setting needs pushing: notif_popup_ms and the two vibrate flags
 * are read straight out of app_settings() at the moment they matter, so they
 * have nothing to apply. The three here are the ones held as state somewhere
 * else.
 */
void applyAll()
{
    qst_hal_set_brightness(s_settings.brightness);
    screen_state_set_timeout_ms((uint32_t)s_settings.screen_timeout_s * 1000u);
    screen_state_set_wrist_wake(s_settings.wrist_wake != 0);
    watch_face_apply((WatchFaceId)s_settings.watch_face);   // no-op before watch_face_begin()
}

// -- storage --------------------------------------------------------------

#ifdef ARDUINO

constexpr const char *NVS_NAMESPACE = "custom_iface";
constexpr const char *NVS_KEY       = "settings";

/// True if a stored blob was read and looked like ours.
bool storageLoad(AppSettings &out)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly*/ true)) {
        return false;
    }
    // Size first, then version: a blob of the right length but an older
    // layout would otherwise be memcpy'd into the wrong fields.
    AppSettings stored = {};
    size_t read = prefs.getBytes(NVS_KEY, &stored, sizeof(stored));
    prefs.end();

    if (read != sizeof(stored) || stored.version != APP_SETTINGS_VERSION) {
        return false;
    }
    out = stored;
    return true;
}

void storageSave(const AppSettings &in)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, /*readOnly*/ false)) {
        return;
    }
    prefs.putBytes(NVS_KEY, &in, sizeof(in));
    prefs.end();
}

#else // !ARDUINO -- native/SDL2 emulator build

#include <stdio.h>

/*There is no NVS on the host, but starting every emulator run from defaults
  would make the load/flush path the one part of this module that never gets
  exercised outside hardware. A file in the working directory stands in.*/
constexpr const char *SETTINGS_PATH = "custom_interface_settings.bin";

bool storageLoad(AppSettings &out)
{
    FILE *f = fopen(SETTINGS_PATH, "rb");
    if (!f) {
        return false;
    }
    AppSettings stored = {};
    size_t read = fread(&stored, 1, sizeof(stored), f);
    fclose(f);

    if (read != sizeof(stored) || stored.version != APP_SETTINGS_VERSION) {
        return false;
    }
    out = stored;
    return true;
}

void storageSave(const AppSettings &in)
{
    FILE *f = fopen(SETTINGS_PATH, "wb");
    if (!f) {
        printf("[settings] could not write %s\n", SETTINGS_PATH);
        return;
    }
    fwrite(&in, 1, sizeof(in), f);
    fclose(f);
}

#endif // ARDUINO

} // namespace

const AppSettings &app_settings(void)
{
    return s_settings;
}

void app_settings_begin(void)
{
    if (!storageLoad(s_settings)) {
        loadDefaults();
        s_dirty = true;     // write the defaults back on the first flush
    }
    applyAll();
}

void app_settings_flush(void)
{
    if (!s_dirty) {
        return;
    }
    storageSave(s_settings);
    s_dirty = false;
}

void app_settings_restore_defaults(void)
{
    loadDefaults();
    // Apply, not just rewrite: leaving the live brightness/timeout/face on
    // their old values would have the device disagreeing with its own settings
    // page until the next reboot.
    applyAll();
    s_dirty = true;
}

// -- setters ---------------------------------------------------------------

void app_settings_set_brightness(int level)
{
    s_settings.brightness = (uint8_t)level;
    qst_hal_set_brightness(level);
    s_dirty = true;
}

void app_settings_set_screen_timeout_s(uint16_t seconds)
{
    s_settings.screen_timeout_s = seconds;
    screen_state_set_timeout_ms((uint32_t)seconds * 1000u);
    s_dirty = true;
}

void app_settings_set_wrist_wake(bool enable)
{
    s_settings.wrist_wake = enable;
    screen_state_set_wrist_wake(enable);
    s_dirty = true;
}

void app_settings_set_watch_face(uint8_t face)
{
    s_settings.watch_face = face;
    watch_face_apply((WatchFaceId)face);
    s_dirty = true;
}

void app_settings_set_notif_popup_ms(uint16_t ms)
{
    s_settings.notif_popup_ms = ms;   // read at popup time; nothing to push
    s_dirty = true;
}

void app_settings_set_vibrate_messages(bool enable)
{
    s_settings.vibrate_messages = enable;
    s_dirty = true;
}

void app_settings_set_vibrate_alerts(bool enable)
{
    s_settings.vibrate_alerts = enable;
    s_dirty = true;
}

void app_settings_set_pinned_mask(uint32_t mask)
{
    s_settings.pinned_mask = mask;   // no subsystem to push to yet
    s_dirty = true;
}

void app_settings_set_low_batt_pct(uint8_t pct)
{
    s_settings.low_batt_pct = pct;   // no subsystem to push to yet
    s_dirty = true;
}

void app_settings_set_lora_enabled(bool enable)
{
    s_settings.lora_enabled = enable;   // no subsystem to push to yet
    s_dirty = true;
}
