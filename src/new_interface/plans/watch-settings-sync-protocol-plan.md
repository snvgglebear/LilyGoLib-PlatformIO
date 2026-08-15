# Plan: watch settings sync (phone -> watch, and back)

Companion to `src/new_interface/`'s home screen, notification-popup, pinned-links
and low-battery-warning features, which today are all watch-local: the user can
change them only by touching the watch. `src/custom_interface/plan.md` asks for
being "able to change watch settings from the phone to the extent possible."
This is the protocol and firmware half of that: a new `settings` message pair,
what it carries, how it persists, and where it plugs into `GbApp`. The
Gadgetbridge-side half is a companion preferences screen; see **Target** below.

**Target:** the `twatch_ultra` branch of
<https://codeberg.org/snvgglebear/Gadgetbridge>, files:

- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraCoordinator.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraConstants.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/twatch_ultra/TWatchUltraDeviceSupport.java`

As with `src/gadgetbridge/android-sms-notifications-plan.md` (this document's
structural template), exact Java/Kotlin file and method names below are
estimates, checked against that branch on 2026-08-15 where noted, and should be
re-verified against the real fork before implementing. Confirmed by fetching
the branch directly: `TWatchUltraCoordinator.getSupportedDeviceSpecificSettings()`
today returns only `R.xml.devicesettings_transliteration` and
`R.xml.devicesettings_autoremove_notifications`, and
`TWatchUltraDeviceSupport` has **no `onSendConfiguration` override at all** —
unlike the SMS plan, which could extend an existing preference-push path, this
one has to add that plumbing from scratch. See §2 and §6.

A second, watch-side caveat: the four UI modules this plan hooks into
(`ui_notification_popup.cpp`, `ui_pinned_links.cpp`, `ui_home.cpp`,
`ui_battery_status.cpp`) are named for their planned role per
`src/new_interface/app_config.h`'s comments and `src/custom_interface/plan.md`,
but do not exist as separate files in this tree yet — today the clock face and
settings sliders live inline in `ui_main.cpp`/`ui_sys.cpp`. Treat the function
names below (`ui_notification_popup_set_timeout_ms()` etc.) as the intended
shape of that split, to be confirmed against whatever those files land as.

---

## 1. What already works

Nothing, in the phone -> watch direction, specifically for settings. Confirmed
by reading `.claude/twatch-ultra-ble-protocol.md` §5/§6 in full: there is no
settings/config message either direction today. The closest thing is §5.11
`vibrate` (`{"t":"vibrate","n":60}`), which is a one-shot "buzz now at this
intensity" command, not a persisted preference, and does not correspond to
anything `gb_platform.h` can actually do (see the haptics ceiling below).

What *is* implemented, and is exactly the kind of thing this plan proposes
exposing, is watch-local and defined once in `src/new_interface/app_config.h`:

| Setting | Constant(s) | Range |
| --- | --- | --- |
| Notification popup timeout | `NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS` | `NOTIFICATION_POPUP_MIN_TIMEOUT_MS`..`NOTIFICATION_POPUP_MAX_TIMEOUT_MS` (2000-15000 ms) |
| Notification vibrate on/off | `NOTIFICATION_POPUP_DEFAULT_VIBRATE` | bool |
| Pinned-apps set | `PINNED_APPS_DEFAULT_MASK` over `enum PinnableApp` | bitmask, `PIN_APP_COUNT` bits |
| Clock face | `CLOCK_MODE_DEFAULT` over `enum ClockMode` | `CLOCK_MODE_DIGITAL` \| `CLOCK_MODE_ANALOG` |
| Low-battery warning threshold | `LOW_BATTERY_WARNING_PERCENT` (+ `..._REARM_PERCENT`) | percent |

These are today either compile-time defaults or, once the on-watch settings UI
lands, runtime values the watch changes for itself. Nothing writes them from
BLE. This plan does not invent new settings beyond this list — it is the
complete set of tunables `app_config.h` documents as user-facing today.

**Persistence precedent already in this codebase.** `src/new_interface/ui_main.cpp`
keeps `brightness_level` in `RTC_DATA_ATTR` storage — survives deep sleep, not a
full power cycle. Separately, and more relevantly here,
`src/new_interface/hal_interface.h`/`.cpp` already has a *real* NVS-backed
settings blob: `user_setting_params_t` (brightness, keyboard backlight, display
timeout, charger current/enable), read with `hw_get_user_setting()` and written
with `hw_set_user_setting()`, backed by the ESP32 `Preferences` library under
the `"pager"` NVS namespace (`hal_interface.cpp:45,81,925-982`). A size-mismatch
check on load already anticipates the struct growing (`prefs.getBytes(...) !=
sizeof(user_setting_params_t)` falls back to defaults), which is exactly the
migration path extending it needs. See §4.

**The haptics ceiling.** `src/new_interface/gadgetbridge_ble/gb_platform.h`
exposes exactly two vibration patterns:

```cpp
enum GbHaptic {
    GB_HAPTIC_TAP = 1,      ///< sharp single click -- new notification
    GB_HAPTIC_ALERT = 47,   ///< long buzz -- incoming call, alarm, "find device"
};
```

`vibrate(GbHaptic effect)` takes one of exactly these two DRV2605 waveform IDs.
There is no intensity or duration parameter anywhere in this layer. So
"vibrate on/off" is a real, implementable setting (whether `GB_HAPTIC_TAP` gets
called at all), but "vibration intensity" or "vibration duration" are not — the
protocol and `GbApp` could carry such fields today, but nothing on the watch
could act on them until the DRV2605 driver layer in `gb_platform.cpp` grows a
richer effect API. That is out of scope for this protocol-only plan; see §6.

## 2. Changes

Phone-side: Gadgetbridge needs a per-device "Watch settings" screen, and a way
to push edits down and reflect the watch's answer back up.

### 2.1 A new device-specific settings screen

`getSupportedDeviceSpecificSettings()` in `TWatchUltraCoordinator.java` today
returns only two `R.xml.devicesettings_*` screens (transliteration,
autoremove-notifications). Add a new one, following whatever XML-preference
pattern other coordinators already use for sliders/switches (`BangleJSCoordinator`
is the go-to reference elsewhere in this fork per the SMS plan) — e.g. a new
`R.xml.devicesettings_twatch_ultra` with:

- a switch for notification-popup vibrate,
- a seek-bar/slider for notification-popup timeout (2-15 s, matching
  `NOTIFICATION_POPUP_MIN_TIMEOUT_MS`/`_MAX_TIMEOUT_MS`),
- a multi-select or per-app checkboxes for the pinned-apps set (7 entries,
  `enum PinnableApp` in `app_config.h`),
- a single-select for clock face (digital/analog),
- a seek-bar for the low-battery warning percent (suggest 5-50%, see §3).

### 2.2 Push on change: `onSendConfiguration`

`TWatchUltraDeviceSupport.java` has **no `onSendConfiguration` override today**
(verified against the branch) — this is new plumbing, not an extension. Add
one that switches on the changed preference key, builds a `settings` JSON
object (§3) containing only the field(s) that changed, and calls the existing
`uartTxJSON(...)` helper already used for every other outbound message in this
class.

### 2.3 Reflect the watch's answer

Because these settings can also change on the watch itself (the on-watch
settings screen `src/custom_interface/plan.md` asks for), a phone-side value
the user hasn't touched should not silently go stale. §3 recommends the watch
echo back its full effective settings state on every change and at connect.
The `onSendConfiguration`-only push in §2.2 does not, by itself, give
Gadgetbridge anywhere to put that echo — nothing in `TWatchUltraDeviceSupport`
currently updates a displayed preference value from an incoming watch message
(battery/version fields are the closest precedent, and those go through
standard GBDevice fields the UI already polls, not a settings screen). This
needs new watch-side handling: parse the incoming `settings` message in the
uartRx dispatcher (alongside `ver`/`status`/etc.), and either (a) cache the
values on the `GBDevice` and have the settings screen read them when opened,
or (b) fire a custom `GBDeviceEvent` the UI layer refreshes on. Flagged as an
open design question for whoever implements the fork side — see §6.

## 3. Protocol additions

New message pair, `settings`, following §10's rule ("add message types freely
... both sides ignore what they do not recognise") and the "every field but
`t` is optional" convention already used for every other phone -> watch
message. `.claude/twatch-ultra-ble-protocol.md` needs both directions added in
the same change that implements this, per its own §10 note.

### 3.1 `settings` — phone -> watch (proposed §5.12)

A partial update: only the fields the phone actually sends are touched.
Sending an empty `{"t":"settings"}` is a valid no-op (useful as a "read
current values" probe once combined with the echo in §3.2 — see the note
there).

| Field | Type | Meaning |
| --- | --- | --- |
| `notif_timeout_ms` | int | Notification popup auto-dismiss delay, milliseconds. Watch clamps to `[NOTIFICATION_POPUP_MIN_TIMEOUT_MS, NOTIFICATION_POPUP_MAX_TIMEOUT_MS]` (2000-15000) |
| `notif_vibrate` | bool | Vibrate (`GB_HAPTIC_TAP`) on notification arrival |
| `pinned_mask` | int | Bitmask over `enum PinnableApp` (`app_config.h`); bits beyond `PIN_APP_COUNT` are masked off |
| `clock_mode` | string | `"digital"` or `"analog"`; unrecognised values are ignored (that field only, not the whole message) |
| `low_batt_pct` | int | Low-battery warning threshold, percent. Watch clamps to `[5, 50]` |

```json
{"t":"settings","notif_timeout_ms":8000,"notif_vibrate":false,"clock_mode":"analog"}
```

That example changes only the timeout, vibrate flag and clock face — pinned
apps and the battery threshold are left exactly as they were, because they are
absent, not because they were sent as their current value.

### 3.2 `settings` — watch -> phone (proposed §6.8)

**Recommended, not optional-to-implement.** Without it, this is a write-only
control: the phone's screen would show whatever the user last set from the
phone, which drifts the moment the user changes something on the watch
itself (which `src/custom_interface/plan.md` explicitly wants to keep
working). Sending the *full* effective state back, rather than echoing just
the fields that changed, sidesteps having to reconcile partial-vs-partial:
the phone always has one authoritative, complete snapshot to render,
regardless of whether the change originated on the phone or the watch.

Send it:
- once at connect (after `ver`, alongside/after `status`), so the settings
  screen has real values to show the moment the user opens it — this is also
  what makes an empty `{"t":"settings"}` probe from §3.1 unnecessary in
  practice, since the watch already announces state on every connect;
- every time any of these five values actually changes, from either origin
  (phone-driven or on-watch), debounced the same way a slider drag should be
  debounced on the sending side (see §6).

| Field | Type | Meaning |
| --- | --- | --- |
| `notif_timeout_ms` | int | Current effective value |
| `notif_vibrate` | bool | Current effective value |
| `pinned_mask` | int | Current effective value |
| `clock_mode` | string | `"digital"` or `"analog"` |
| `low_batt_pct` | int | Current effective value |

```json
{"t":"settings","notif_timeout_ms":6000,"notif_vibrate":true,"pinned_mask":71,"clock_mode":"digital","low_batt_pct":20}
```

(`71` = `0b1000111` = `PIN_WIRELESS | PIN_ALARMS | PIN_LORA | PIN_SETTINGS`,
the default mask in `app_config.h`.)

## 4. Persistence interaction

**Recommendation: real NVS storage via the existing `user_setting_params_t`
pattern, not `RTC_DATA_ATTR`.** `RTC_DATA_ATTR` (as used for `brightness_level`
in `ui_main.cpp`) only survives deep sleep — a dead battery, a firmware
update, or a full power-off loses it silently. That is an acceptable ceiling
for a value the watch picked as a local UI convenience (screen brightness
ramps back up from a sensible floor either way), but it is a worse fit for a
value the user explicitly set *from their phone*: losing it silently and
reverting to firmware defaults is more surprising when the user remembers
having configured it deliberately, possibly weeks earlier, than when it is
just "the watch's own idea of a default."

Concretely: extend `user_setting_params_t` (`hal_interface.h`) with the five
fields from §3 (store `clock_mode` as the existing `enum ClockMode`'s
underlying `uint8_t`, not as a string — the string is a wire-format choice,
not a storage one), and extend `hw_get_user_setting()`/`hw_set_user_setting()`
(`hal_interface.cpp`) to carry them. The struct already has a working
migration path for exactly this: `hal_interface.cpp`'s load path resets to
defaults whenever `prefs.getBytes(...)` returns a size that does not match
`sizeof(user_setting_params_t)`, which is precisely what happens the first
time a watch with old firmware boots new firmware with a longer struct. No new
NVS namespace or blob is needed — `"pager"` (`NVS_NAME` in `hal_interface.cpp`)
already exists and is already the single settings blob for this app's other
user-tunable knobs.

## 5. Watch-side integration point

Mirrors the existing `onAlarms()` / `GB_CHANGE_ALARMS` / `alarms()` shape in
`gb_app.h`/`gb_protocol.h` exactly, plus one addition alarms don't need: since
`GbApp` doesn't own where these five values live (the UI modules do — `GbApp`
is deliberately LVGL-free per its own file comment), it needs a way to mirror
their *current* state in order to build the full-state echo in §3.2, not just
receive updates.

**`gb_protocol.h`** — new struct, new handler hook, new outbound builder:

```cpp
/// §5.12 `settings` (proposed). Every field is independently optional; the
/// has_* flags say which ones the phone actually sent, so a partial update
/// only touches what changed.
struct GbSettings {
    bool has_notif_timeout_ms = false;
    int32_t notif_timeout_ms = 0;       ///< ms; handler clamps to [2000, 15000]

    bool has_notif_vibrate = false;
    bool notif_vibrate = false;

    bool has_pinned_mask = false;
    uint32_t pinned_mask = 0;           ///< bit i set = PinnableApp i pinned

    bool has_clock_mode = false;
    std::string clock_mode;             ///< "digital" | "analog"

    bool has_low_batt_pct = false;
    int32_t low_batt_pct = 0;           ///< percent; handler clamps to [5, 50]
};
```

```cpp
// GbProtocolHandler gains:
virtual void onSettings(const GbSettings &) {}   ///< §5.12 `settings`
```

```cpp
// New outbound builder, mirroring gb_msg_status()'s style:
// §6.8 `settings` echo -- always the full effective state (never partial),
// so one message is enough for the phone to repaint its screen regardless
// of which side the change came from.
std::string gb_msg_settings(int32_t notif_timeout_ms, bool notif_vibrate,
                             uint32_t pinned_mask, const std::string &clock_mode,
                             int32_t low_batt_pct);
```

**`gb_app.h`/`gb_app.cpp`** — one new `GbStateChange` value, one incoming
handler (same shape as `onAlarms()`), plus report*() mirror setters the UI
modules call after they apply a change (from either origin) so the echo stays
accurate:

```cpp
enum GbStateChange {
    ...
    GB_CHANGE_SETTINGS,    ///< a phone-driven settings update arrived; see settings()
};

class GbApp : public GbProtocolHandler {
public:
    // Incoming (phone -> watch), read by listeners during GB_CHANGE_SETTINGS:
    const GbSettings &settings() const { return m_incoming_settings; }

    // Mirror setters -- called by whichever UI module owns each field, once
    // applied, whether that application was phone-driven or a local edit on
    // the watch's own settings screen. Each call re-sends the full echo
    // (§3.2), debounced.
    void reportNotificationSettings(int32_t timeout_ms, bool vibrate);
    void reportPinnedMask(uint32_t mask);
    void reportClockMode(const std::string &mode);
    void reportLowBatteryPercent(int32_t pct);

protected:
    void onSettings(const GbSettings &settings) override;   // stores + notify(GB_CHANGE_SETTINGS)

private:
    GbSettings m_incoming_settings;
    // Mirrored effective state, seeded from app_config.h defaults at boot and
    // overwritten by report*() as the owning UI modules initialize from NVS
    // and thereafter as either side changes something:
    int32_t m_eff_notif_timeout_ms = NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS;
    bool m_eff_notif_vibrate = NOTIFICATION_POPUP_DEFAULT_VIBRATE;
    uint32_t m_eff_pinned_mask = PINNED_APPS_DEFAULT_MASK;
    std::string m_eff_clock_mode = "digital";
    int32_t m_eff_low_batt_pct = LOW_BATTERY_WARNING_PERCENT;
    void sendSettingsEcho();   // builds gb_msg_settings() from m_eff_*, debounced
};
```

`onConnected()` (already a private hook in `gb_app.h`) additionally calls
`sendSettingsEcho()` once, so a freshly-paired phone gets real values
immediately rather than only after the first change.

**No new plumbing beyond this is needed on the watch side.** The fan-out
`app_gadgetbridge.cpp` already provides (`app_gb_add_listener()`,
`GbStateChange` dispatch) is exactly the seam the four owning UI modules use:

- `ui_notification_popup.cpp` — on `GB_CHANGE_SETTINGS`, if
  `gb_app.settings().has_notif_timeout_ms` or `.has_notif_vibrate`, call its
  existing `ui_notification_popup_set_timeout_ms()` / `_set_vibrate()`
  setters, persist via the extended `hw_set_user_setting()` (§4), then call
  `gb_app.reportNotificationSettings(...)`.
- `ui_pinned_links.cpp` — same, for `has_pinned_mask`, masking off any bits
  `>= PIN_APP_COUNT` before applying, then `gb_app.reportPinnedMask(...)`.
- `ui_home.cpp` — same, for `has_clock_mode`, mapping the string to
  `enum ClockMode` (unrecognised string -> no-op, per §3.1), then
  `gb_app.reportClockMode(...)`.
- `ui_battery_status.cpp` — same, for `has_low_batt_pct`, clamped to
  `[5, 50]`, then `gb_app.reportLowBatteryPercent(...)`.

Each module also calls its own `report*()` once at boot, right after loading
its NVS-persisted value, so `GbApp`'s mirror (and therefore the connect-time
echo) is correct even before the phone has ever sent anything.

## 6. Order of work

1. Add `settings` §5.12/§6.8 to `.claude/twatch-ultra-ble-protocol.md`
   (field tables + JSON examples from §3 above) — the spec is the contract;
   land it in the same change as the code per the doc's own §10.
2. `gb_protocol.h`/`.cpp`: `GbSettings`, `GbProtocolHandler::onSettings()`,
   the `"settings"` branch in the dispatch switch (mirroring the `"alarm"`
   branch's per-field validation style), `gb_msg_settings()`.
3. `gb_app.h`/`.cpp`: `GB_CHANGE_SETTINGS`, `onSettings()` override,
   `m_incoming_settings`/`m_eff_*` fields, the four `report*()` setters,
   `sendSettingsEcho()` (debounced), call it from `onConnected()`.
4. `hal_interface.h`/`.cpp`: extend `user_setting_params_t` with the five
   fields, defaults in the reset-to-defaults path, getters/setters plumbed
   through `hw_get_user_setting()`/`hw_set_user_setting()`.
5. Wire up the four UI modules per §5's list — each is a small, independent
   change and can land/be tested one at a time.
6. Gadgetbridge fork: `devicesettings_twatch_ultra` screen + coordinator
   registration (§2.1), new `onSendConfiguration()` (§2.2), incoming-`settings`
   handling to refresh the screen (§2.3, needs its own design pass — flagged
   as open in §7).
7. Manual test pass per §7 test matrix, on the emulator (`gb_link_stdio.cpp`
   can drive `settings` from a terminal same as any other message) and on
   hardware.

Steps 1-5 are independently useful and testable without the phone at all,
using the emulator's stdio transport to hand-type `settings` lines. Step 6 is
the only part that needs the real Android fork.

## 7. Test matrix

| Case | Expected |
| --- | --- |
| `{"t":"settings","notif_vibrate":false}` | Only vibrate changes; timeout, pinned mask, clock mode, battery threshold untouched |
| Set `notif_timeout_ms` to 500 (below floor) | Clamped to 2000, not rejected outright |
| Set `notif_timeout_ms` to 60000 (above ceiling) | Clamped to 15000 |
| Set `low_batt_pct` to 0 | Clamped to 5 |
| Set `low_batt_pct` to 90 | Clamped to 50 |
| Set `clock_mode` to `"sundial"` | That field ignored; other fields in the same message still applied; no crash |
| Set `pinned_mask` with bit 30 set (`1u<<30`) | High bit masked off before applying; watch does not crash indexing `PinnableApp` |
| Every field set on a fully-populated device | Watch's next echo reflects all five, matching what was sent (after clamping) |
| Phone sets a value, then watch reboots (full power cycle, battery removed) | Value survives — NVS via `user_setting_params_t`, not RTC memory |
| Phone sets a value, watch goes to deep sleep and wakes | Value survives (would have anyway even under `RTC_DATA_ATTR`; NVS is a superset) |
| User changes clock face on the watch itself | Next connect (or the debounced immediate echo, if already connected) shows the new value on the phone, not the last phone-set one |
| Phone and watch change the same field within the same second | Last write wins; the echo converges within one debounce interval; no crash or stuck state |
| Reconnect after a settings change while disconnected | Fresh connect's echo (§3.2, sent from `onConnected()`) carries the current value |
| Rapid slider drag on the phone (many `settings` messages) | Watch applies the final value; NVS is not written on every intermediate one if debounced per §6 risk below |

## 8. Risks

- **Haptics ceiling.** As covered in §1: `GbHaptic` only has two fixed
  waveforms (`GB_HAPTIC_TAP`, `GB_HAPTIC_ALERT`). `notif_vibrate` (on/off) is
  fully supported by this plan; vibration *intensity* or *duration* are not,
  and would need `gb_platform.cpp`'s DRV2605 layer extended first — out of
  scope here, but a real ceiling on how far "make vibration phone-tunable"
  can go without more work.
- **Files this plan hooks into don't exist yet.** `ui_notification_popup.cpp`,
  `ui_pinned_links.cpp`, `ui_home.cpp` and `ui_battery_status.cpp` are named
  from `app_config.h`'s comments and `plan.md`'s feature list, not from files
  present in this tree today (confirmed: `ui_main.cpp`/`ui_sys.cpp` currently
  hold this logic inline). Re-check the function names in §5 against whatever
  those files actually export once they land.
- **The fork has no push-preference plumbing today.** Confirmed against the
  branch: `TWatchUltraDeviceSupport` has no `onSendConfiguration` at all, and
  no existing pattern for an incoming watch message updating a *displayed*
  preference value (battery/version are shown elsewhere in the UI, not on a
  settings screen). §2.3/§6 step 6 both flag the echo-consumption side as
  needing its own design pass on the fork, not just a JSON parse.
- **NVS wear.** A phone-side slider firing `settings` on every drag tick would
  write NVS on every message if applied naively. Debounce on both ends: the
  phone should debounce `onSendConfiguration` the way `BangleJSCoordinator`-style
  seek-bar prefs typically do (commit on release, not per-tick), and
  `GbApp::sendSettingsEcho()` (§5) should coalesce bursts rather than writing
  + notifying on every incoming message.
- **`pinned_mask` forward-compatibility.** `PIN_APP_COUNT` will likely grow as
  more apps become pinnable. A mask saved by an older phone build simply
  leaves the new bits at 0 (unpinned) — harmless — but a *newer* phone talking
  to *older* firmware could set bits the old firmware doesn't know about; §5's
  masking (`>= PIN_APP_COUNT` bits dropped) prevents that from corrupting
  anything, at the cost of the phone-side change silently not taking effect
  for those bits until the watch firmware updates.
- **`clock_mode` as a string, not the raw `enum ClockMode` int.** Chosen
  deliberately, matching the existing `call.cmd`/`musicstate.state` string-enum
  style rather than `alarm`'s numeric fields: a future third clock face is
  just a new string value, with no risk of a phone and watch disagreeing on
  what integer `2` means after an out-of-sync firmware/app update. The
  trade-off is a typo (`"digitial"`) silently doing nothing rather than
  failing loudly — acceptable given §2's per-field-not-whole-message error
  handling, but worth the phone-side UI using a fixed picker, not free text.
