# Plan: settings-sync follow-ups

Tracks what remains after the reserved-fields pass-through (delta plan §2) was
implemented. Written against `settings-sync-delta-plan.md`; this file is a
punch-list, not a re-derivation. The three items in §1 are the tail of that
plan (its own §4-§5); §2 restates the still-deferred `settings-page.md`
expansion so it does not get lost.

## 0. What just landed (context)

The wire is now symmetric for the three reserved fields:

- `AppSettings` v2 carries `pinned_mask`/`low_batt_pct`/`lora_enabled` with
  defaults from `app_config.h`, setters in `settings/app_settings.cpp`, and
  the on-disk size/version guard already migrates a v1 blob by discarding it.
- `GbSettings` in `gb_protocol.h`, the dispatcher in `gb_protocol.cpp`, and
  `gb_msg_settings()` (now 6-arg) all parse and emit the three fields.
- `GbApp::onSettings()` in `gb_app.cpp` applies them (with the
  `[APP_LOW_BATT_MIN_PCT, APP_LOW_BATT_MAX_PCT]` clamp on `low_batt_pct`);
  `sendSettingsEcho()` includes them in every echo.
- Gadgetbridge gained `PREF_TWATCH_LORA_ENABLED`, the customizer
  registration, the `handleUartRxJSON`/`onSendConfiguration` branches, the
  `SwitchPreferenceCompat`, and the two strings. `pinned_mask`/`low_batt_pct`
  were already wired end-to-end on the phone side.

Nothing about the wire shape or the storage layout is still open — only the
follow-ups below.

## 1. Immediate — close out the delta plan

### 1.1 Update the protocol spec document

`.claude/twatch-ultra-ble-protocol.md` §5.14 / §6.8 list the six fields, but
never state that three of them are *persisted-and-echoed pass-through* on the
`custom_interface` watch rather than acted on. §10 covers the general "unknown
fields are ignored" contract, which is the wrong bucket: the watch is not
ignoring these — it stores them and echoes them, it just does not yet let any
subsystem read them. That is a subtler contract worth naming so a future
firmware author does not remove the storage thinking it is dead code, and so a
future third-party client knows the echo is meaningful (the watch's stored
value) even before any behaviour hangs off it.

Change: one short paragraph after the §5.14 field list, phrased something like
"On firmwares where a field has no subsystem consumer yet, the watch still
persists it to NVS and echoes it back in §6.8. Compliance with §5.14 is
receive-and-persist; behaviour is a separate contract per feature."

Cross-reference `plans/settings-sync-delta-plan.md` §2 for the rationale
(three reasons, in decreasing order of importance) so the spec does not have
to re-argue it.

Nothing else in the doc needs to move — the field tables, the debounced-echo
note in §6.8, and the "watch's echo is ground truth" rule in §3.2 all continue
to hold as written.

### 1.2 Emulator test pass (delta plan §5, section A)

Run `pio run -e emulator_watch_ultra -t exec` with `src_dir = src/custom_interface`
in `platformio.ini`, feed the four hand-typed lines from delta plan §5, and
verify against the echo the watch prints back that:

- Each partial update leaves other fields unchanged.
- `{"t":"settings","low_batt_pct":0}` clamps to 5 in the echo;
  `{"t":"settings","low_batt_pct":90}` clamps to 50.
- The combined `{"t":"settings","pinned_mask":71,"low_batt_pct":15,"lora_enabled":true}`
  produces one echo after the 500 ms debounce carrying the full effective
  state (all nine fields — six original + three reserved).
- Kill and restart the emulator: `custom_interface_settings.bin` in the cwd
  round-trips the three new values (this is the emulator's stand-in for NVS
  from `settings/app_settings.cpp:122`).
- `{"t":"settings"}` (empty object) applies nothing, does not crash, and does
  not emit an echo (nothing changed, so debounce discards).

If any of the above diverges from what the echo says, the bug is somewhere in
`gb_protocol.cpp`'s dispatcher branch, `gb_app.cpp:onSettings()`, or the
`AppSettings` migration — not in the design.

### 1.3 Hardware test pass (delta plan §5, hardware footnote)

On the T-Watch Ultra, one extra step the emulator does not exercise: pull
power, cold-boot, reconnect the phone, and confirm the first echo after
reconnect reflects what was last set. That validates the NVS path in
`storageLoad()`/`storageSave()` against the `Preferences` namespace
`custom_iface` on real flash, which the emulator's `fopen()` cannot stand in
for (different failure modes: NVS init errors, wear-level move mid-write,
etc.). Not blocking; useful before anyone starts building the low-battery
warning on top of the stored value.

## 2. Deferred — Section B (settings-page.md expansion)

Still gated on the watch-side settings page being reachable from the launcher
(`gadgetbridge-button-grid-nav.md`). Nothing has changed about the design in
delta plan §3: four new keys (`brightness_pct`, `screen_timeout_s`,
`wrist_wake`, `vibrate_alerts`), no `AppSettings` version bump because all
four fields already exist in the struct, `clock_mode` continues to carry
`watch_face`.

The only reason this waits is order-of-operations: the phone has no need for
these knobs before the watch has a place to originate a change and echo it
back. Once the settings page lands, delta plan §3.2-§3.3 spells out the
mechanical additions — four `has_*`/value pairs, four parse branches, four
setters already exist (`app_settings_set_brightness()` etc. push into
`qst_hal`/`screen_state` today), four Gadgetbridge preference entries. No
open design questions.

Recommend leaving this plan file as the tracker for §2 until the settings
page lands, then folding it into the settings-page implementation PR rather
than opening a third plan file for the same wire.

## 3. Non-todos (called out so they do not sneak in later)

- **New listener event.** `GB_CHANGE_SETTINGS` already fires from
  `onSettings()`; the three reserved fields have no UI consumer, so no new
  event is needed and adding one now would be premature.
- **Range-check echo.** The watch clamps `low_batt_pct` on receive, so the
  stored (and echoed) value is always in range. Adding a symmetric clamp in
  `gb_msg_settings()` would just be duplicated logic.
- **A "pass-through" flag on the wire.** The watch does not advertise which
  fields it *acts on* vs merely stores — and it should not. The phone treats
  a valid echo as the ground truth per protocol §3.2, and inventing a
  behaviour manifest would drift out of sync with the code the moment a
  feature landed. Documentation (§1.1) is the right layer.
