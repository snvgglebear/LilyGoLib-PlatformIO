# Plan: turn-by-turn navigation, phone-computes / watch-displays

Implements the `src/custom_interface/plan.md` line "navigation functionality,
including displaying directions and estimated time of arrival with
openstreetmap integration." There is currently zero navigation/map code in
this repo. The architecture is decided and out of scope to revisit: **the
phone (via whatever turn-by-turn app the user already runs — OsmAnd, Google
Maps, etc.) computes the route; the watch only receives and displays small
JSON text updates** over the existing Gadgetbridge protocol, the same way
`weather` (§5.9) and `call` (§5.5) already work. The watch never fetches map
tiles, never routes on-device, and needs no network or storage budget for
maps.

**Target:** the `twatch_ultra` branch of
<https://codeberg.org/snvgglebear/Gadgetbridge>, files:

- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/twatch_ultra/TWatchUltraDeviceSupport.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraCoordinator.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraConstants.java`

These are the same three files `src/gadgetbridge/android-sms-notifications-plan.md`
targets, and `TWatchUltraDeviceSupport.onNotification()` quoted there (verified
against the fork on 2026-08-10) is real, confirming the file and its shape.
**The exact method signatures below are not verified against the fork** —
I cannot read that repo from this environment. They are reconstructed from
upstream Gadgetbridge (`Freeyourgadget/Gadgetbridge`, the codebase the fork is
based on), whose `NavigationInfoSpec` model, `DeviceSupport.onSetNavigationInfo()`
callback, `GoogleMapsNotificationHandler`, and Bangle.js `nav` wire message I
fetched and read directly (see citations inline below). Verify against the
actual fork source before implementing — the model class and callback name are
almost certainly identical (they're core Gadgetbridge, not per-device code),
but the fork's `TWatchUltraCoordinator`/`TWatchUltraDeviceSupport` method
bodies are estimates in the same spirit as the SMS plan's.

---

## 1. What already works

The watch-side infrastructure this builds on, all already in this repo:

- **Protocol plumbing.** `src/new_interface/gadgetbridge_ble/gb_protocol.h`/`.cpp`
  already do newline framing, JSON decode/encode, and per-line error isolation
  (§2 of `.claude/twatch-ultra-ble-protocol.md`) — a new `t` value is a decode
  branch plus a struct, nothing else changes.
- **`GbApp` state/listener pattern.** `gb_app.h` already has the exact shape a
  `nav` feature needs: `onCall()`/`GB_CHANGE_CALL`/`callActive()`/`call()` is a
  four-part idiom (protocol hook -> state enum -> boolean-active accessor ->
  data accessor) repeated for every phone-pushed feature (`onWeather()` /
  `GB_CHANGE_WEATHER` / `weather()` is the closer analog for "no ack needed,
  just display the latest snapshot"; `onCall()` additionally models
  active/inactive state, which `nav` also needs). A navigation addition is a
  fifth instance of the same idiom, not a new pattern.
- **Decoupled UI seam.** `src/new_interface/app_gadgetbridge.h`'s
  `app_gb_add_listener()` already lets independent UI modules react to `GbApp`
  state changes without knowing about each other; a future `ui_navigation.cpp`
  registers a listener exactly like any other module would.
- **Safe-area layout.** `lib/usable_area/src/usable_area.h`'s
  `usable_area_place(parent, y, height)` / `usable_area_rect(parent)` already solve
  "lay out a banner or screen inside the curved bezel" — a nav banner or screen
  is just another caller.
- **Haptics.** `gb_platform.h`'s `GbHaptic` (`GB_HAPTIC_TAP` — single click,
  `GB_HAPTIC_ALERT` — long buzz) is what `onCall()`/`onNotify()` already use
  and what a new-maneuver tap would reuse.
- **Extensibility contract.** §10 of the protocol doc: new `t` values are
  additive, unknown ones are dropped by both sides, nothing existing changes.
  This plan adds exactly one such addition (plus its companion "end" message,
  same shape as `notify`/`notify-`, §5.3/§5.4).

## 2. Changes (Android side, conceptual)

### 2.1 Where the data actually comes from — verify this first

The plan.md line says "with openstreetmap integration," and there's a real,
already-existing Gadgetbridge mechanism for that: **Gadgetbridge already talks
to OsmAnd(+) directly.** Per Gadgetbridge's own docs
(<https://gadgetbridge.org/basics/integrations/navigation/>), Gadgetbridge
"autodetects which version [of OsmAnd] is installed and connects to the app's
API in the background" — this is a structured API integration, not
notification scraping, and OsmAnd's routing is OSM-data-based, which is the
most literal reading of "openstreetmap integration" available without the
watch doing anything OSM-related itself. **Recommend this as the primary
source** for this feature, ahead of Google Maps.

The second, broader-reach mechanism is real too: Gadgetbridge's
`NotificationListenerService`-based listener
(`externalevents/NotificationListener.java`) already parses **Google Maps'
persistent navigation notification** into the same internal model via
`GoogleMapsNotificationHandler`
(`externalevents/notifications/GoogleMapsNotificationHandler.java`), which "
identifies navigation actions by matching the images Google Maps displays on
notifications using a fuzzy matching approach." This requires the user to
allow Maps notifications in Gadgetbridge's own notification-access settings,
and has one documented failure mode worth flagging to users: Google Maps'
**"Live Updates" notification style breaks the scrape** — it must be disabled
in Android Settings -> Apps -> Maps -> Notifications for turn/distance data to
come through at all.

**I do not have confirmed evidence that Gadgetbridge natively supports Waze.**
The task brief mentions it as a possible source; treat that as unverified and
check the fork/upstream for a `WazeNotificationHandler` or equivalent before
promising it in user-facing docs.

Both paths funnel into the same internal model and (per upstream source)
the same `DeviceSupport` callback —
`onSetNavigationInfo(NavigationInfoSpec navigationInfoSpec)` — so **implementing
that one method in `TWatchUltraDeviceSupport` should pick up both sources for
free**, without writing an OsmAnd- or Maps-specific line of code. This needs
confirming against the fork (the callback is core Gadgetbridge, so it should
already be wired to whatever base class `TWatchUltraDeviceSupport` extends),
but if true it collapses most of "phone-side work" to one method + one mapping
function.

**Explicit non-goal:** the phone app does not run its own router (no OSRM, no
self-hosted OSM routing). It relays whatever the user's own nav app — OsmAnd
(OSM-based) or Google Maps — has already computed. This keeps the Android
change small and avoids duplicating routing logic Gadgetbridge doesn't have
today.

The real upstream `NavigationInfoSpec`
(`app/src/main/java/nodomain/freeyourgadget/gadgetbridge/model/NavigationInfoSpec.java`,
fetched from Codeberg) has this shape:

```java
public class NavigationInfoSpec {
    public static final int ACTION_CONTINUE = 1;
    public static final int ACTION_TURN_LEFT = 2;
    // ... slight/sharp left+right variants fill 3..7 (exact ordinals unconfirmed) ...
    public static final int ACTION_TURN_RIGHT_SHARPLY = 7;
    public static final int ACTION_KEEP_LEFT = 8;
    public static final int ACTION_KEEP_RIGHT = 9;
    public static final int ACTION_UTURN_LEFT = 10;
    public static final int ACTION_UTURN_RIGHT = 11;
    public static final int ACTION_OFFROUTE = 12;
    public static final int ACTION_ROUNDABOUT_RIGHT = 13;
    public static final int ACTION_ROUNDABOUT_LEFT = 14;
    public static final int ACTION_ROUNDABOUT_STRAIGHT = 15;
    public static final int ACTION_ROUNDABOUT_UTURN = 16;
    public static final int ACTION_FINISH = 17;
    public static final int ACTION_MERGE = 18;

    public String instruction;
    public String distanceToTurn;   // pre-formatted, e.g. "300 m" or "0.2 mi"
    public int nextAction;          // one of the ACTION_ constants
    public String ETA;              // pre-formatted, e.g. "08:39"
    public int completionPercent;
}
```

The important, easy-to-miss detail: **`distanceToTurn` and `ETA` are already
display-formatted strings**, not raw numbers — that's what Google Maps' own
notification text already looks like, and it's what OsmAnd's API most likely
hands over too (unconfirmed, but the field type says so). There is no reliable
raw-meters value sitting on the Android side for the notification-scrape path.
§3 designs the wire message around this reality rather than pretending clean
numeric fields exist everywhere.

### 2.2 Declare navigation support on the coordinator

Upstream Gadgetbridge gates most optional per-device features behind a
`supportsX()` boolean on the device's `DeviceCoordinator`. Verify the actual
method name (likely `supportsNavigation()`) and add an override returning
`true` in `TWatchUltraCoordinator.java` — without it, `DeviceCommunicationService`
probably never calls `onSetNavigationInfo()` for this device at all, even if
notifications are being scraped successfully.

### 2.3 Implement `onSetNavigationInfo()`

In `TWatchUltraDeviceSupport.java`, following the exact style of the already
verified `onNotification()` (quoted in `android-sms-notifications-plan.md` §1):

```java
@Override
public void onSetNavigationInfo(final NavigationInfoSpec navigationInfoSpec) {
    final JSONObject o = new JSONObject();
    o.put("t", "nav");
    o.put("action", mapAction(navigationInfoSpec.nextAction));
    o.put("instr", navigationInfoSpec.instruction);
    o.put("dist", navigationInfoSpec.distanceToTurn);
    o.put("eta", navigationInfoSpec.ETA);
    if (navigationInfoSpec.completionPercent > 0) {
        o.put("pct", navigationInfoSpec.completionPercent);
    }
    uartTxJSON("onSetNavigationInfo", o);
}

private static String mapAction(final int nextAction) {
    switch (nextAction) {
        case NavigationInfoSpec.ACTION_CONTINUE:            return "continue";
        case NavigationInfoSpec.ACTION_TURN_LEFT:            return "left";
        case NavigationInfoSpec.ACTION_TURN_RIGHT_SHARPLY:   return "sharp_right";
        case NavigationInfoSpec.ACTION_KEEP_LEFT:            return "keep_left";
        case NavigationInfoSpec.ACTION_KEEP_RIGHT:           return "keep_right";
        case NavigationInfoSpec.ACTION_UTURN_LEFT:           return "uturn_left";
        case NavigationInfoSpec.ACTION_UTURN_RIGHT:          return "uturn_right";
        case NavigationInfoSpec.ACTION_OFFROUTE:             return "offroute";
        case NavigationInfoSpec.ACTION_ROUNDABOUT_RIGHT:     return "roundabout_right";
        case NavigationInfoSpec.ACTION_ROUNDABOUT_LEFT:      return "roundabout_left";
        case NavigationInfoSpec.ACTION_ROUNDABOUT_STRAIGHT:  return "roundabout_straight";
        case NavigationInfoSpec.ACTION_ROUNDABOUT_UTURN:     return "roundabout_uturn";
        case NavigationInfoSpec.ACTION_FINISH:               return "arrive";
        case NavigationInfoSpec.ACTION_MERGE:                return "merge";
        // fill in the remaining slight-turn constants once their exact
        // ordinals are confirmed against the live NavigationInfoSpec.java
        default: return "unknown";
    }
}
```

Wire `action` as a **string**, not the raw `int`, deliberately: it decouples
the watch firmware from Gadgetbridge's internal enum ordinals (which this plan
could not fully confirm — see the `// exact ordinals unconfirmed` comment
above), matches how the rest of this protocol already prefers self-describing
strings (`cmd`/`state`/`n` fields throughout §5/§6) over magic numbers, and
matches the precedent of Gadgetbridge's own Bangle.js/wasp-os integration,
which sends navigation as
`{"t":"nav","instr":"High St towards Null St","distance":966,"action":"continue","eta":"08:39"}`
(fetched from <https://www.espruino.com/Gadgetbridge>, the page
`.claude/twatch-ultra-ble-protocol.md` itself points to as the precedent this
whole protocol mirrors) — i.e. Gadgetbridge already ships a string-keyed `nav`
message design for a different device, and §3 below is deliberately close to
it.

### 2.4 Signal navigation end

**Not confirmed against source:** exactly how/when upstream Gadgetbridge
learns navigation has stopped (Maps notification cancelled? `onSetNavigationInfo`
called with a cleared spec? a separate callback?). Check
`NotificationListener.java` / `GoogleMapsNotificationHandler.java` for how a
dismissed or replaced Maps notification is handled, and whether OsmAnd's API
sends an explicit "navigation stopped" event. Whatever that signal turns out
to be, translate it into sending the bare `nav-` message (§3.2) so the watch
clears its display — do not rely on the phone screen going stale, since old
Maps text could otherwise sit on the watch for the rest of the day.

## 3. Protocol additions

Both additive and optional per §10 of the protocol doc. **Update
`.claude/twatch-ultra-ble-protocol.md` in the same change** as the firmware —
that document is the contract, same rule the SMS plan states.

### 3.1 `nav` — turn-by-turn update (phone -> watch, new §5.12)

One active route at a time (matches every source above — there is no
multi-route concurrency to design for). Send on every instruction change and,
if the source provides it, on distance ticks; the watch just displays the
latest snapshot, same as `weather`.

| Field | Type | Meaning |
| --- | --- | --- |
| `action` | string | Maneuver: `continue`, `left`, `right`, `slight_left`, `slight_right`, `sharp_left`, `sharp_right`, `keep_left`, `keep_right`, `uturn_left`, `uturn_right`, `roundabout_left`, `roundabout_right`, `roundabout_straight`, `roundabout_uturn`, `offroute`, `arrive`, `merge`, or `unknown` |
| `instr` | string | Instruction text, e.g. `"Turn left onto Main St"` |
| `dist` | string | Pre-formatted distance to the maneuver, e.g. `"300 m"`, `"0.2 mi"` — display verbatim, do not reparse |
| `dist_m` | int | Raw meters to the maneuver, **only when the source actually has it** (e.g. a structured API like OsmAnd's, as opposed to Maps' notification text). Omit rather than guess; the watch falls back to `dist` when absent |
| `eta` | string | Pre-formatted ETA or remaining time, e.g. `"08:39"`, `"12 min"` |
| `dest` | string | Destination name/address, if known |
| `pct` | int | Route completion percent, 0-100, mirrors `NavigationInfoSpec.completionPercent` |

```json
{"t":"nav","action":"left","instr":"Turn left onto Main St","dist":"300 m","dist_m":300,"eta":"08:39","dest":"128 Bristol Rd","pct":42}
```

Every field but `t` is optional per the protocol's general rule (§5 preamble);
a firmware or phone build that only has `instr`+`action` still produces a
usable display, same tolerance `weather` already has.

### 3.2 `nav-` — navigation ended (phone -> watch, new §5.13)

Mirrors `notify-`'s role (§5.4): clear the display. No fields — there is only
ever one active route, so no id is needed, unlike `notify-`'s `id`.

```json
{"t":"nav-"}
```

### 3.3 `GbApp` / `gb_protocol` additions (watch side, this repo)

In `src/new_interface/gadgetbridge_ble/gb_protocol.h`, a new struct next to
`GbWeather` and a new pair of hooks next to `onWeather()`/`onFind()`:

```cpp
/// §5.12 `nav`. dist/eta are pre-formatted by the phone -- see the plan doc
/// for why raw meters/epoch aren't reliably available from most sources.
struct GbNavigation {
    bool valid = false;
    std::string action = "unknown";
    std::string instr;
    std::string dist;
    int32_t dist_m = -1;     ///< -1 when the phone didn't provide raw meters
    std::string eta;
    std::string dest;
    int32_t pct = -1;        ///< -1 when unknown
};
```

```cpp
virtual void onNavigation(const GbNavigation &) {}   ///< §5.12 `nav`
virtual void onNavigationEnd() {}                    ///< §5.13 `nav-`
```

Decoding in `gb_protocol.cpp`, in the same `if (t == "weather") { ... }` chain
that already exists (around line 175), following that block's exact
`doc["field"] | default` idiom:

```cpp
} else if (t == "nav") {
    GbNavigation nav;
    nav.valid = true;
    nav.action = str_of(doc["action"]);
    if (nav.action.empty()) nav.action = "unknown";
    nav.instr = str_of(doc["instr"]);
    nav.dist = str_of(doc["dist"]);
    nav.dist_m = doc["dist_m"] | -1;
    nav.eta = str_of(doc["eta"]);
    nav.dest = str_of(doc["dest"]);
    nav.pct = doc["pct"] | -1;
    handler.onNavigation(nav);

} else if (t == "nav-") {
    handler.onNavigationEnd();
```

In `src/new_interface/gadgetbridge_ble/gb_app.h`, mirroring `GB_CHANGE_CALL` /
`callActive()` / `call()` exactly:

```cpp
enum GbStateChange {
    ...
    GB_CHANGE_ALARM_FIRED,
    GB_CHANGE_NAVIGATION,        ///< nav update arrived, or navigation ended
};
```

```cpp
/// True while a navigation session is active (nav received, nav- not yet).
bool navigationActive() const { return m_navigation_active; }
const GbNavigation &navigation() const { return m_navigation; }
```

```cpp
protected:
    ...
    void onNavigation(const GbNavigation &nav) override;
    void onNavigationEnd() override;

private:
    ...
    GbNavigation m_navigation;
    bool m_navigation_active = false;
```

In `gb_app.cpp`, next to `onWeather()`/`onCall()`:

```cpp
void GbApp::onNavigation(const GbNavigation &nav)
{
    // Tap once when the maneuver actually changes, not on every distance
    // tick -- Maps/OsmAnd can push updates every few seconds and a nav
    // session that buzzed on each one would be unbearable. Mirrors the
    // single GB_HAPTIC_TAP onNotify() already uses for "something new
    // happened," just gated on a state transition instead of arrival.
    bool new_leg = !m_navigation_active || nav.action != m_navigation.action;
    m_navigation = nav;
    m_navigation_active = true;
    if (new_leg) {
        gb_platform::vibrate(GB_HAPTIC_TAP);
    }
    notify(GB_CHANGE_NAVIGATION);
}

void GbApp::onNavigationEnd()
{
    m_navigation_active = false;
    notify(GB_CHANGE_NAVIGATION);
}
```

This is the same four-part idiom as `call`/`weather`, so it should not need
new architecture in `gb_app.cpp` beyond these two methods.

### 3.4 Non-goal: watch -> phone nav messages

No watch-initiated "cancel navigation" or "reroute" message in this plan —
neither the Bangle.js precedent nor upstream Gadgetbridge's model has one, and
adding a `nav` §6.x watch->phone entry (`{"t":"nav","n":"cancel"}`, mirroring
`call`'s §6.5 shape) is a clean, backwards-compatible follow-up if a future
build wants a physical "stop navigation" button, but it is out of scope here.

## 4. Watch-side UI (design only — for a future `ui_navigation.cpp`)

Not building this; describing the integration point concretely so an
implementer doesn't have to re-derive the architecture.

- **Module shape.** A `ui_navigation.cpp` exporting an `app_t` (same pattern
  as `ui_gps.cpp`'s `ui_gps_main`, registered in `ui_main.cpp` via
  `create_app()`) for a dedicated full-screen view, *plus* a small always-on
  banner usable from the home screen while a navigation session is active —
  this repo already has that split in spirit (compare how `ui_gps.cpp` is a
  launchable app while `plan.md` separately wants persistent notification
  popups on the home screen). Register with `app_gb_add_listener()` and switch
  on `change == GB_CHANGE_NAVIGATION`; read state with `gb_app.navigationActive()`
  and `gb_app.navigation()` — never touch `GbApp` internals or the BLE layer
  directly, matching every other UI module's contract with `GbApp`.
- **Layout.** A `usable_area_place(parent, y, height)` band (or `usable_area_rect()`
  for the full-screen variant): maneuver icon on the left, `instr` as the
  primary (largest) label, `dist` beneath it, `eta`/`dest` as a smaller
  trailing line. Prefer `dist_m`-derived rendering only if present; otherwise
  display the `dist`/`eta` strings verbatim — do not attempt to reparse
  Gadgetbridge's pre-formatted text into numbers, that's the whole reason §3.1
  carries both a string and an optional raw field.
- **Asset gap.** `src/new_interface/images/` (and `src/factory/images/`, which
  it was copied from) has `img_compass.png`/`img_compass_needle.png`, but
  those are already spoken for by the existing Compass app
  (`ui_compass_main`, registered in `ui_main.cpp` against `img_compass`) and
  are generic heading-needle graphics, not maneuver arrows. **There is no
  turn-arrow icon set (left/right/slight/sharp/u-turn/roundabout/arrive/merge)
  anywhere in this repo.** Options for a future implementer: commission/find a
  small arrow icon set and `LV_IMG_DECLARE` it the way `ui_main.cpp` does for
  every other app icon, or — cheaper for an MVP — draw the arrows
  procedurally with LVGL primitives (`lv_line`/rotated `lv_obj` triangles) so
  the sixteen-odd `action` values don't block on art. `img_gps.png` (already
  present, currently the GPS app's launcher icon) is a reasonable off-the-shelf
  choice for the *launcher* icon for this new app in `ui_main.cpp`'s grid —
  that's a different, easier problem than the in-session maneuver icon.
- **Cleared on `nav-`.** `navigationActive() == false` after `GB_CHANGE_NAVIGATION`
  should hide the banner/screen and, if it was the foreground screen, return to
  the caller the way `gb_ui`-style call screens dismiss on `call` `end`/`reject`.

## 5. Order of work

1. Verify §2.1/§2.2's Android-side assumptions against the actual fork source:
   does `onSetNavigationInfo()` exist and fire for this device already
   (contingent on a `supportsNavigation()`-style coordinator flag), what
   OsmAnd/Maps notification-access setup the user needs, and how navigation
   end is actually signalled (§2.4).
2. Add `GbNavigation`, `onNavigation()`/`onNavigationEnd()` to
   `gb_protocol.h`/`.cpp` (§3.3) — compiles and is testable stand-alone, no
   BLE or LVGL dependency, same as every other message type.
3. Add `GB_CHANGE_NAVIGATION`, the accessors, and the two `GbApp` overrides
   (§3.3) — testable via the native/stdio `gb_link_stdio.cpp` path
   (`src/gadgetbridge/README.md`'s emulator harness) by typing `nav`/`nav-`
   JSON lines at the terminal, no watch UI required yet.
4. Update `.claude/twatch-ultra-ble-protocol.md` with new §5.12/§5.13 entries
   (this is the contract; do this before or alongside Android work, not after).
5. Android: `onSetNavigationInfo()` + `mapAction()` in
   `TWatchUltraDeviceSupport.java`, coordinator flag in
   `TWatchUltraCoordinator.java` (§2.2-2.3).
6. Android: wire the navigation-end signal found in step 1 to send `nav-`
   (§2.4).
7. Watch UI: build `ui_navigation.cpp` per §4, starting with the procedural-
   arrow fallback so it isn't blocked on art assets; register it in
   `ui_main.cpp` and via `app_gb_add_listener()`.
8. Icon set (real maneuver artwork) as a follow-up polish pass, not a blocker.

Steps 2-4 are useful and shippable (protocol + state machine, testable in the
emulator) even if steps 5-8 slip; steps 5-6 need the fork and cannot be
verified from this repo.

## 6. Test matrix

| Case | Expected on the watch |
| --- | --- |
| `nav` with all fields | Banner/screen shows action icon, instruction, distance, ETA, destination |
| `nav` with only `action`+`instr` (minimal source) | Still renders; distance/ETA lines simply blank, no crash |
| Unknown `action` value (future Gadgetbridge action, or typo) | Falls back to a generic/neutral icon, per §10's "unknown values are tolerated" rule — never a crash |
| Rapid `nav` updates (every 1-2s, distance ticking down) | Text updates smoothly; **no** haptic tap per update (only on `action` change, per §3.3) |
| `action` changes leg-to-leg (e.g. `continue` -> `left`) | Single `GB_HAPTIC_TAP`, icon and instruction swap |
| `nav-` received | Banner/screen clears immediately; `navigationActive()` false |
| `nav-` received with no prior `nav` (phone restarts mid-connection) | No-op, no crash — matches `notify-` on an unknown id today |
| Malformed `nav` line (bad JSON, missing `t`) | Dropped per §2 framing rules; does not desync the line parser or affect the next message |
| Oversized `nav` line (huge `instr`/`dest`) | Line >8192 bytes discarded per existing `GbLineAssembler` behavior; next line still parses |
| BLE disconnect mid-navigation | Watch keeps last-known state on screen (matches how `weather`/`call` behave today) until reconnect delivers fresh state or `nav-` |
| Phone screen off during navigation | No effect — both OsmAnd's background API link and the NotificationListener path run without the screen on; BLE notify is unaffected by phone screen state |
| Two `nav` messages with different `action` but same `instr` (e.g. GPS jitter near a turn) | Watch treats each independently; a single flapping tap between two real states is an accepted, not a bug, per §3.3's simple "did action change" rule |
| Full route: several `nav` updates then `nav-` at arrival (`action":"arrive"`) | Icon/instruction reflect `arrive` on the final update; `nav-` still clears it afterward |

## 7. Risks

- **`onSetNavigationInfo()`'s exact existence/signature, and whether a
  `supportsNavigation()`-style flag gates it, are unverified against the fork.**
  This is the single biggest unknown in this plan (§2.1-2.2); if the callback
  or capability flag don't exist as named, the Android-side estimate in §2.3
  needs adjusting, though the watch-side protocol/state work (§3) doesn't
  depend on it and can proceed regardless.
- **Navigation-end signal is unconfirmed** (§2.4). If it turns out Gadgetbridge
  has no clean "nav stopped" event for the notification-scrape path (only a
  timeout or the notification simply going stale), the watch may need its own
  staleness timeout (e.g. clear the display if no `nav` arrives for N seconds)
  as a defensive fallback rather than relying solely on `nav-`.
  This mirrors the `android-sms-notifications-plan.md`'s own dismiss-loop risk
  in spirit — a signal from the phone can be missing or delayed.
- **`NavigationInfoSpec.nextAction`'s exact ordinal-to-name mapping is
  incomplete** (§2.3's `mapAction()` — several slight-turn constants between
  `ACTION_TURN_LEFT=2` and `ACTION_TURN_RIGHT_SHARPLY=7` were not confirmed).
  Low risk in practice: an unmapped ordinal falls through to `"unknown"`,
  which §6's test matrix already requires the watch to render safely, so a
  wrong/missing mapping degrades gracefully rather than breaking anything.
- **Google Maps "Live Updates" silently breaks the scrape path.** This is a
  real, documented Gadgetbridge limitation, not specific to this device.
  Worth a line in whatever end-user setup docs this feature gets, since the
  failure mode ("navigation just doesn't show up") gives no error anywhere.
- **Waze support is unconfirmed.** Don't promise it in user-facing copy until
  verified against the fork/upstream; if the user actually wants Waze,
  scraping its notification would need a new handler analogous to
  `GoogleMapsNotificationHandler`, which is real Android-side work this plan
  does not scope.
- **`dist`/`eta` as opaque display strings** means the watch cannot do its own
  unit conversion or countdown math — it can only show what the phone already
  formatted. This is a deliberate tradeoff (§2.1/§3.1) given what the primary
  source (Maps notification text) actually provides, but it does mean e.g. a
  "walking vs. driving units" mismatch between the phone's locale and the
  watch's own settings isn't something firmware can paper over.
- **Icon asset gap** (§4) could tempt a shortcut of reusing `img_compass` for
  maneuver display; don't — it's already the Compass app's icon
  (`ui_compass_main` in `ui_main.cpp`) and reusing it for a different, semantically
  unrelated purpose would be confusing in the app grid and on-screen alike.
