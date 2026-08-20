# Plan: settings-sync delta — reserved fields + the settings-page.md expansion

Companion to `watch-settings-sync-protocol-plan.md`. That plan defined the
`settings` message pair and specified six fields; three are already implemented
end-to-end (`notif_timeout_ms`, `notif_vibrate`, `clock_mode`), three are
specified but unimplemented on the watch (`pinned_mask`, `low_batt_pct`,
`lora_enabled`), and `settings-page.md` has since added five more local-only
settings that don't yet cross the wire (`brightness`, `screen_timeout_s`,
`wrist_wake`, `vibrate_alerts`, and the analog/digital `watch_face` choice
which reused `clock_mode`).

This plan is the delta: what to do about the six-vs-three gap now, and what
protocol shape the five new settings should take when they eventually cross
over. It does **not** re-derive the framing, debouncing, persistence pattern
or Gadgetbridge-side plumbing — that's all in the parent plan and, for the
five settings-page.md fields, in `settings-page.md` §11. Cross-references
rather than duplication.

**Terminology.** "Parent plan" below means `watch-settings-sync-protocol-plan.md`.
That plan is written against `new_interface` (a different app whose UI never
landed in this tree); wherever it names `src/new_interface/...` files or
`user_setting_params_t`, the `custom_interface` equivalents are in
`custom_interface/settings/app_settings.{h,cpp}` (versioned `AppSettings`
struct, dirty-tracked, `Preferences` under `NVS_NAMESPACE = "custom_iface"`)
and `custom_interface/settings/settings_screen.cpp`. The mechanism is the
same, only the file names differ.

## 1. Where things stand

**Watch (`custom_interface/gadgetbridge_ble/`).** The `settings` machinery is
already there: `GbSettings` in `gb_protocol.h:106-115`, dispatcher branch in
`gb_protocol.cpp:193-207`, `gb_msg_settings()` builder in
`gb_protocol.h:234-235` / `gb_protocol.cpp:311-320`, `onSettings()` handler
in `gb_app.cpp:332-372`, `sendSettingsEcho()` (debounced 500 ms) in
`gb_app.cpp:527-545`, connect-time force-echo in `gb_app.cpp:113`. What's
missing is the three fields' worth of parsing, encoding, applying and
echoing.

**Gadgetbridge (`Gadgetbridge/app/src/main/.../twatch_ultra/`).** Fully wired
for the parent plan's five original fields — see `TWatchUltraConstants.java:88-118`
(all five pref keys defined), `TWatchUltraSettingsCustomizer.java:54-60` (all
five preference handlers registered), `TWatchUltraDeviceSupport.java:283-305`
(incoming `settings` parse), `:602-638` (`onSendConfiguration` per-key
switch), and `devicesettings_twatch_ultra.xml` (5 preferences). What's
missing is `lora_enabled`.

**Net today:** Gadgetbridge already sends `pinned_mask` and `low_batt_pct`
whenever the user edits the corresponding preference; the watch's dispatcher
silently drops them (the JSON keys aren't in the `if` chain), so the phone's
preference screen shows values the watch never received, and the watch never
echoes them back — the fields are effectively write-only, into `/dev/null`.
`lora_enabled` doesn't cross the wire from either side yet.

## 2. Section A — reserved-fields pass-through

The three fields are `pinned_mask`, `low_batt_pct`, `lora_enabled`. In
`custom_interface` today, none of them has a receiver:

- **`pinned_mask`.** No app-pinning concept — the launcher grid
  (`gb_ui.cpp:1006-1013`) is a fixed 5-tile array (Watch/Chats/Alerts/Music/Settings).
- **`low_batt_pct`.** No low-battery warning code. The quick-settings tray
  and the watch faces just render `batteryPercent()` as-is.
- **`lora_enabled`.** No LoRa code at all — `custom_interface` doesn't
  compile any radio driver, and the `ARDUINO_T_LORA_PAGER` occurrences in
  `screen_state.{h,cpp}` and `gb_platform.cpp` are pure board-select
  `#ifdef`s that only pick between button configurations. The
  `lora-meshtastic-protocol-interop-plan.md` §10 feature the parent plan
  presupposes has never landed.

### 2.1 Why "receive, persist, echo" and not "silent drop until the feature exists"

Three reasons in decreasing order of importance:

1. **Gadgetbridge already sends two of them.** The phone-side pref for
   pinned-apps and low-battery works, thinks it worked, and shows the value
   the user set. The watch drops it. Reconnect the same phone to a factory-fresh
   watch and the phone will show its cached (unshipped) value forever, since
   the parent plan's rule ("watch's echo is the ground truth for the phone
   screen", §3.2) can't rescue a value the watch never stored. Pass-through
   fixes this without waiting for the underlying features: the watch stores
   what the phone sent, echoes it back on the next connect, and the two ends
   agree even though nothing on the watch consumes the value yet.
2. **The feature owner, later, only writes the effect side.** When someone
   actually builds low-battery warnings, they read `app_settings().low_batt_pct`
   and act on it. They don't have to touch the protocol layer, the parser,
   the echo builder, or `GbApp::onSettings()`. Same for pinning and LoRa.
3. **Version-bump cost is one-off.** `AppSettings` grows by 6 bytes
   (uint32 + uint8 + uint8, no padding at the tail past `notif_popup_ms`'s
   uint16 alignment), `APP_SETTINGS_VERSION` goes 1→2, and the size/version
   check in `app_settings.cpp:95-97` discards old blobs — the only observed
   effect is a one-time reset to defaults on first boot after the update,
   which is invisible because the defaults match the shipping values.

### 2.2 Watch-side changes

**`custom_interface/app_config.h`** — three defaults, one range:

```c
// -- Reserved (phone-synced pass-through, no watch consumer yet -- see
//    plans/settings-sync-delta-plan.md §2).
constexpr uint32_t APP_PINNED_MASK_DEFAULT   = 0;   ///< no apps pinned
constexpr uint8_t  APP_LOW_BATT_DEFAULT_PCT  = 20;
constexpr uint8_t  APP_LOW_BATT_MIN_PCT      = 5;   ///< matches Gadgetbridge's clamp
constexpr uint8_t  APP_LOW_BATT_MAX_PCT      = 50;
constexpr bool     APP_LORA_ENABLED_DEFAULT  = false;
```

Bounds match `TWatchUltraConstants.java:115-117` deliberately — same clamp
on both ends means neither side can push a value the other will silently
rewrite.

**`custom_interface/settings/app_settings.h`** — bump version, extend struct,
three setters:

```c
constexpr uint16_t APP_SETTINGS_VERSION = 2;    // was 1: added the three reserved fields

struct AppSettings {
    uint16_t version;
    uint8_t  brightness;
    uint8_t  wrist_wake;
    uint16_t screen_timeout_s;
    uint8_t  watch_face;
    uint8_t  vibrate_messages;
    uint8_t  vibrate_alerts;
    uint16_t notif_popup_ms;

    // Reserved: persisted and echoed to the phone, but no watch feature
    // consumes them yet. See plans/settings-sync-delta-plan.md §2.
    uint32_t pinned_mask;
    uint8_t  low_batt_pct;
    uint8_t  lora_enabled;
};

void app_settings_set_pinned_mask(uint32_t mask);
void app_settings_set_low_batt_pct(uint8_t pct);
void app_settings_set_lora_enabled(bool enable);
```

**`custom_interface/settings/app_settings.cpp`** — `loadDefaults()` seeds the
three new fields; `applyAll()` is unchanged (nothing to push); three setters
mirror the pattern of `set_vibrate_messages()` (update live copy, mark
dirty, no push).

### 2.3 Watch-side protocol changes

**`gb_protocol.h`** — three new fields on `GbSettings` (has-flag + value),
and the note at lines 100-104 flips from "not carried" to "carried
pass-through; no watch consumer yet":

```cpp
struct GbSettings {
    // ... existing three ...

    bool has_pinned_mask = false;
    uint32_t pinned_mask = 0;

    bool has_low_batt_pct = false;
    int32_t low_batt_pct = 0;    ///< handler clamps to [5, 50]

    bool has_lora_enabled = false;
    bool lora_enabled = false;
};
```

`gb_msg_settings()` signature grows three parameters (in the same order):

```cpp
std::string gb_msg_settings(int32_t notif_timeout_ms, bool notif_vibrate,
                            const std::string &clock_mode,
                            uint32_t pinned_mask, int32_t low_batt_pct,
                            bool lora_enabled);
```

Not overloaded — the old signature has one caller (`gb_app.cpp:542`) and
this is the same call, extended.

**`gb_protocol.cpp`** — three parse branches in the `"settings"` case
(mirroring the existing three); three additional `doc[...] = ...` lines in
`gb_msg_settings()`.

For `pinned_mask` on the ArduinoJson side, prefer
`doc["pinned_mask"].is<uint32_t>()` (which also matches `int32_t` values in
practice — ArduinoJson's numeric `.is<>` is permissive), because
`Gadgetbridge` sends it as a Java `int` and that shows up as int32 on the
wire.

### 2.4 Watch-side handler changes

**`gb_app.cpp:332` `onSettings()`** — three more `if (settings.has_*)`
blocks, each calling the matching setter and setting `changed = true`.
`low_batt_pct` clamps to `[APP_LOW_BATT_MIN_PCT, APP_LOW_BATT_MAX_PCT]`
before applying, same way `notif_timeout_ms` clamps today.

**`gb_app.cpp:542` `sendSettingsEcho()`** — pass the three new fields from
`app_settings()` into the extended `gb_msg_settings()` call.

That is the whole watch-side change: no new file, no new subsystem hook, no
new listener notification (`GB_CHANGE_SETTINGS` fires the same way; UI has
nothing to refresh because nothing on the UI reads these three fields).

### 2.5 Gadgetbridge-side changes (`lora_enabled` only)

The other two are already wired end-to-end on the phone side. Only
`lora_enabled` is new:

- **`TWatchUltraConstants.java`** — one new key
  (`PREF_TWATCH_LORA_ENABLED = "pref_twatch_lora_enabled"`); update the
  class-level doc-comment message catalog (lines 46, 54) to list
  `lora_enabled` alongside the other setting fields.
- **`TWatchUltraSettingsCustomizer.java`** — one more
  `handler.addPreferenceHandlerFor(PREF_TWATCH_LORA_ENABLED)` in
  `customizeSettings()`.
- **`TWatchUltraDeviceSupport.java`** — one branch in the incoming `settings`
  parser (line 283) reading `json.getBoolean("lora_enabled")` into the pref;
  one `case` in `onSendConfiguration()` (line 602) building
  `o.put("lora_enabled", prefs.getBoolean(...))`.
- **`res/xml/devicesettings_twatch_ultra.xml`** — one `SwitchPreferenceCompat`.
- **`res/values/strings.xml`** — `pref_title_twatch_lora_enabled`,
  `pref_summary_twatch_lora_enabled`.

Off by default on both sides (fail-closed, matching the parent plan §10.2
and the watch's `APP_LORA_ENABLED_DEFAULT = false`).

### 2.6 What §2 buys, in one line per field

- `pinned_mask`: Gadgetbridge's pinned-apps multi-select stops disappearing
  into the void; ready for the feature to land.
- `low_batt_pct`: same, for the low-battery seek-bar.
- `lora_enabled`: exists at all, on both ends, off by default, ready for
  whoever wires the meshtastic plan up.

## 3. Section B — the settings-page.md expansion (deferred)

`settings-page.md` adds five local settings not currently in the protocol:

| Setting | `AppSettings` field | Reachable from | Currently synced? |
|---|---|---|---|
| Brightness | `brightness` (uint8) | settings screen + quick-settings tray | No |
| Screen timeout | `screen_timeout_s` (uint16, 0 = never) | settings screen | No |
| Wrist-raise wake | `wrist_wake` (bool) | settings screen (Ultra/Pager only) | No |
| Vibrate on calls/alarms | `vibrate_alerts` (bool) | settings screen | No |
| Watch face | `watch_face` (uint8 `WatchFaceId`) | settings screen | Yes — as `clock_mode` string |

The last row is a naming collision, not a real gap: the parent plan's
`clock_mode` covers today's two faces (`WATCH_FACE_DIGITAL`, `WATCH_FACE_ANALOG`)
and the mapping in `gb_app.cpp:357-363` already round-trips them. What's
"deferred" is only whether a *third* face is added and, if so, whether the
protocol keeps the string-enum shape (parent plan §8 argued for it) or grows
past it.

### 3.1 What to add when it lands

**Fields.** Four new keys on the wire, one existing key kept as-is:

```
brightness_pct    int  0-100      (not a raw qst_hal level -- see below)
screen_timeout_s  int  0-180      (0 = never sleep)
wrist_wake        bool
vibrate_alerts    bool
clock_mode        string          (unchanged; still covers watch_face)
```

**Naming.** `brightness_pct` rather than `brightness` because the raw
`qst_hal_set_brightness()` range is board-dependent (0-255 on the Ultra,
different elsewhere) and a percentage is the one representation both ends
can agree on without knowing which board is on the other end. The
Gadgetbridge-side pref would drive a seek-bar with `min=1, max=100` and the
watch would map to `qst_hal_brightness_min() + (span * pct / 100)` in
`onSettings()`. Match the existing tray/quick-settings behaviour, which
already treats brightness as a percentage of the same span.

`vibrate_alerts` deliberately distinct from `notif_vibrate` — the parent
plan mapped `notif_vibrate` to `GB_HAPTIC_TAP` (messages) only, and the
settings-page.md distinction between messages and alerts (calls/alarms/find-device)
is real. Adding one bool preserves that split.

`wrist_wake` is a no-op on the S3 (which has no BHI260AP), matching the
watch's own settings screen (settings-page.md §4 hides it conditionally).
The pref should be visible on the phone regardless: hiding it there would
require the phone to know which board is on the other end, which it doesn't,
and setting it while the watch can't honour it is harmless (the setter is a
no-op without `HAS_WRIST_TILT_SENSOR`, per settings-page.md §6.1).

### 3.2 Watch-side changes when it lands

Same shape as §2, four fields more:

- `GbSettings` grows four `has_*`/value pairs
- `gb_protocol.cpp` dispatcher gains four parse branches
- `gb_msg_settings()` gains four arguments and four `doc[] =` lines
- `GbApp::onSettings()` gains four `if (has_*)` blocks, calling the
  existing `app_settings_set_*()` setters (which already push into the
  right subsystem — brightness into `qst_hal`, timeout into
  `screen_state`, etc.)
- `sendSettingsEcho()` passes four more fields from `app_settings()`

No struct-version bump: `AppSettings` already carries all four values from
day one of settings-page.md. The delta is purely on the protocol edge.

### 3.3 Gadgetbridge-side changes when it lands

Four new preferences in `devicesettings_twatch_ultra.xml` (one seek-bar,
one seek-bar, two switches), four new constants in
`TWatchUltraConstants.java`, four new `handler.addPreferenceHandlerFor()`
calls, four new parse branches in `handleUartRxJSON` (line 283), four new
`case` arms in `onSendConfiguration` (line 602), four new string pairs.

Mirrors §2.5 exactly, at four times the size — no design decisions left.

### 3.4 Why "deferred" and not "do it now"

Two reasons: (1) the watch-side settings page from settings-page.md is not
yet built (`settings/settings_screen.cpp` exists but isn't in the launcher
grid until gadgetbridge-button-grid-nav.md lands), so there is nothing on
the watch to originate a change and validate the echo path against; (2) all
four are strictly additive over §2 and share no other design decision with
it, so batching them wastes review scope on both plans. Do §2 first, land
the settings page, then §3 becomes rote plumbing.

## 4. Step order

1. §2 all at once: `app_config.h` defaults, `app_settings.{h,cpp}`
   extension, `gb_protocol.{h,cpp}` extension, `gb_app.cpp` handler
   extension. Bump `APP_SETTINGS_VERSION` in the same change as the struct
   growth — the version guard in `app_settings.cpp:95` covers the migration.
2. Android §2.5: `TWatchUltraConstants` + customizer + device support +
   XML + strings. Independent of §1 in mechanism (they meet on the wire),
   but only useful *with* §1 landed — otherwise the phone still writes into
   `/dev/null`, as before.
3. Update `.claude/twatch-ultra-ble-protocol.md`'s message catalog to note
   the reserved-but-persisted status of the three fields on the watch side
   (§10 of that doc already covers the "unknown fields ignored" rule, but
   "we ignore this field's *effect* but do persist it" is a subtler
   contract worth naming).
4. §3 lands with the settings page, per settings-page.md §11.

## 5. Testing

Emulator first, `pio run -e emulator_watch_ultra -t exec`; the stdio
transport in `gb_link_stdio.cpp` accepts `settings` lines by hand.

Section A:

```
{"t":"settings","pinned_mask":71}
{"t":"settings","low_batt_pct":15}
{"t":"settings","lora_enabled":true}
{"t":"settings","pinned_mask":71,"low_batt_pct":15,"lora_enabled":true}
```

Expectations, verified against the echo line the watch prints back:

- Each partial update leaves other fields as they were.
- `low_batt_pct` of 0 clamps to 5; of 90 clamps to 50 (Gadgetbridge does
  the same clamp, so watch-clamp is a belt-and-suspenders for the
  hand-typed and third-party-client cases).
- After a `settings` message, the debounce elapses, and one echo line goes
  out carrying the full effective state — all nine fields (six original
  + three reserved).
- Kill and restart the emulator: values survive via
  `custom_interface_settings.bin` in the cwd, and the reconnect echo
  reflects them.
- Send `{"t":"settings"}` (empty): no fields applied, no crash, no echo
  (nothing changed, debounce discards).

Section A on hardware — one extra check: pull power on the Ultra, reboot,
reconnect the phone, confirm the echo reflects what was last set (NVS via
`Preferences`, `NVS_NAMESPACE = "custom_iface"`).

Section B: no testing yet — the code isn't landing.
