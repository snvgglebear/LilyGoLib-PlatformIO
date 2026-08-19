# Plan: a Settings page for custom_interface

A settings screen modelled on `src/factory/ui_sys.cpp`, reachable from exactly
two places -- a tile on the Gadgetbridge grid launcher and a gear in the
swipe-down quick-settings tray -- and deliberately *not* part of the tab swipe
chain.

This is the first screen in `custom_interface` that has to persist anything, so
roughly half the work is the settings store and the runtime knobs behind the
controls, not the LVGL page itself. Section 6 is the honest list of that.

## 1. Examples this is built from

Everything below already exists in this repo; nothing here is invented.

| Source | What it contributes |
|---|---|
| [`src/factory/ui_sys.cpp`](../../factory/ui_sys.cpp) | The model for the whole page: `lv_menu` with one row per subpage, a `local_param` working copy, commit on exit, plus the System Info and Devices-status read-only pages. |
| [`src/factory/ui_tools.cpp:279-395`](../../factory/ui_tools.cpp) | `create_text()` / `create_slider()` / `create_switch()` / `create_button()` / `create_label()` -- the settings-row factories to port. `create_menu()` at :457. |
| [`src/factory/ui_define.h:112-127`](../../factory/ui_define.h) | Their documented contracts, worth copying verbatim into the port's header. |
| [`src/factory/hal_interface.cpp:925-990`](../../factory/hal_interface.cpp) | The persistence pattern: one packed struct, `prefs.getBytes()` with a size check, defaults on mismatch, and a separate `#else` branch of plain defaults for the emulator. |
| [`src/new_interface/ui_sys.cpp:242-265`](../../new_interface/ui_sys.cpp) | How a domain subpage gets bolted onto the same page (`create_subpage_lora()`), including a "why is this off right now" note label. |
| [`src/new_interface/app_config.h:130-214`](../../new_interface/app_config.h) | Defaults *and* min/max in one header: `NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS`, `_MIN_`/`_MAX_`, `NOTIFICATION_POPUP_DEFAULT_VIBRATE`, `CLOCK_MODE_*`. Exactly the knobs `plan.md` asks the settings panel to expose. |
| [`src/new_interface/hal_interface.h:323-342`](../../new_interface/hal_interface.h) | The extended `user_setting_params_t` -- what a settings blob looks like once notification/clock/pinned fields are added to the factory five. |
| `lvgl@9.2.2/examples/widgets/menu/lv_example_menu_5.c` | LVGL's own canonical multi-page settings menu (root back button + sections), which `create_menu()` is a thin wrapper around. |
| [`quick_settings_tray/`](../quick_settings_tray/) | This app's live-brightness control and its `qst_hal_*` shim -- the settings page must share that HAL, not grow a second one. |
| [`gadgetbridge_ble/gb_ui.cpp`](../gadgetbridge_ble/gb_ui.cpp) + [`gb_ui_metrics.h`](../gadgetbridge_ble/gb_ui_metrics.h) | This app's own idiom: local widget helpers in an anonymous namespace, finger-sized tap targets from one metrics header, everything built inside `usable_area_rect()`. |

The important structural difference from factory: `src/factory` has
`ui_define.h`/`ui_tools.cpp`/`hal_interface.*` as an app-wide framework, and
`custom_interface` has none of that. Every helper the page needs must be ported
into `custom_interface` rather than included.

## 2. What is actually settable today

| Setting | Backed by | Missing |
|---|---|---|
| Display brightness | `qst_hal_get/set_brightness()`, `_min()/_max()` | nothing -- works today, only needs persisting |
| Screen timeout | `SCREEN_SLEEP_TIMEOUT_MS`, a `#define` at [`screen_state.cpp:4`](../screen_state/screen_state.cpp#L4) | runtime variable + setter |
| Wrist-raise wake | `HAS_WRIST_TILT_SENSOR` block in `screen_state.cpp` | an enable flag the detector honours |
| Watch face (digital/analog) | `simple_face_init()` / `batman_dial_init()`, chosen by a commented-out line at [`app_setup.cpp:91-92`](../app_setup.cpp#L91-L92) | teardown for each face + a registry |
| Notification popup duration | `maybeShowMessagePopup()` in `gb_ui.cpp` | the popup has no auto-dismiss timer at all |
| Vibrate on notification / on call | `gb_platform::vibrate()`, called from 9 places in `gb_app.cpp` | a gate those calls pass through |
| Firmware / board / battery / link info | `GB_FW_VERSION`, `gb_platform::hardwareName()`, `batteryPercent()`, `gb_link_device_name()` | nothing -- read-only rows |

So the page ships four *live* settings, three *new* behaviours, and one
info page.

## 3. New files

```
src/custom_interface/
  app_config.h                    <- NEW: defaults, ranges, row metrics (per plan.md's
                                        "all constants in a single location")
  settings/
    app_settings.h/.cpp           <- NEW: the persisted struct + store + accessors
    settings_widgets.h/.cpp       <- NEW: create_menu/create_text/create_slider/
                                        create_switch/create_label, ported from
                                        src/factory/ui_tools.cpp
    settings_screen.h/.cpp        <- NEW: the lv_menu page itself
```

`settings_widgets` is a port, not a copy: drop the `lv_group_add_obj()` calls
(this app is touch-only -- see 8.5), drop the `LV_MENU_ITEM_BUILDER_VARIANT_2`
stacked variant if the rows read fine without it, and size rows from
`app_config.h` the way `gb_ui.cpp` sizes buttons from `gb_ui_metrics.h`.

## 4. The settings tree

`lv_menu` main page, one row per subpage, root back button enabled -- factory's
`ui_sys_enter()` shape:

```
Settings
 +-- Display & Backlight
 |     Brightness            slider, min..max from qst_hal, applies live
 |     Screen timeout        slider 0..180 s, 0 = never  ("%u s" / "never")
 |     Wrist-raise wake      switch          [only if HAS_WRIST_TILT_SENSOR]
 +-- Watch Face
 |     Analog face           switch: off = simple_face, on = batman_dial
 +-- Notifications
 |     Popup duration        slider 2..15 s
 |     Vibrate: messages     switch  -> gates GB_HAPTIC_TAP
 |     Vibrate: calls/alarms switch  -> gates GB_HAPTIC_ALERT
 +-- System Info             read-only labels (see below)
 +-- Restore defaults        button -> confirm msgbox -> defaults + reboot-free re-apply
```

**Switches, not dropdowns, on purpose.** Factory uses `create_dropdown()`
freely, but an open `lv_dropdown` list is a floating box sized to its options
and is not routed through `usable_area`; on the Ultra's curved glass a list
opened near a corner can render under the bezel. With two watch faces, a switch
says the same thing and cannot overflow. Keep `create_dropdown()` out of the
initial port and revisit only if a genuinely multi-valued setting appears.

**System Info rows** (factory's `create_subpage_info()`, minus what this app has
no HAL for): firmware `GB_FW_VERSION`, board `gb_platform::hardwareName()`, BLE
name `gb_link_device_name()` and link state, battery percent / volts / charging,
LVGL version via `lv_version_*()`, build time `__DATE__ " " __TIME__`, and free
heap on Arduino. A 1 Hz `lv_timer` refreshes the live ones exactly as
`sys_timer_event_cb()` does -- and, like factory, must be deleted when the page
closes.

## 5. Entry points, and the one that is excluded

**Grid tile.** [`plans/gadgetbridge-button-grid-nav.md`](gadgetbridge-button-grid-nav.md)
defines the launcher as a 2x2 of `GbGridEntry{icon, name, tab}`. Settings is the
first entry that opens a *screen* rather than a tab, so that struct needs an
action rather than an index:

```c
struct GbGridEntry {
    const char *icon;
    const char *name;
    uint32_t    tab;        ///< tab to open, or GB_TAB_NONE
    void      (*open)(void);///< non-NULL instead: run this (settings_screen_open)
};
```

The grid becomes 2 columns x 3 rows (5 tiles, one cell empty; put Settings last
so the hole is bottom-right). Re-running that plan's sizing: Ultra tiles go from
~156x182 to ~156x118, S3 from ~108x94 to ~108x60. Fine on the Ultra, tight but
usable on the S3 -- worth checking there first.

**Tray gear.** The tray's footer band ([`quick_settings_tray.cpp:209-218`](../quick_settings_tray/quick_settings_tray.cpp#L209-L218))
currently holds only the collapse grabber. Add a gear button beside it:

```c
void settingsButtonCb(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_settings_tray_close();   // the tray lives on lv_layer_top() -- see 8.3
    settings_screen_open();
}
```

To avoid `quick_settings_tray.cpp` depending on the settings module, the tray
takes a callback instead: `quick_settings_tray_set_action(cb)`, wired in
`setupGui()`. The tray stays a leaf module, as it is today.

**Not a tab.** Settings is `lv_screen_load()`-ed, so it is outside the tabview
entirely and the swipe chain stays Grid -> Watch -> Chats -> Alerts -> Music.
This also keeps the settings page free to use left/right for its own purposes
later, which a tab page can never do.

**Getting back.** `create_menu()`'s root back button, which on subpages goes up
one level and on the main page exits (factory's `back_event_handler()`). Exit
returns to the screen that was active when Settings was opened -- captured in
`settings_screen_open()` -- so the grid tile returns to the Gadgetbridge screen
and the tray gear returns to whatever it was opened over. Swipe-down on the
settings screen opens the tray, matching every other non-watchface screen.

## 6. Enablement work behind each control

The page is easy; these are the changes that make its switches mean something.

**6.1 `screen_state`: timeout and wrist-raise become runtime state.**
`SCREEN_SLEEP_TIMEOUT_MS` is a compile-time `#define` used at two call sites
(the Arduino and native `manageSleepState()` branches). Replace with a static
`s_timeout_ms`, seeded from `app_config.h`'s default, plus:

```c
void screen_state_set_timeout_ms(uint32_t ms);   ///< 0 disables the idle timeout
uint32_t screen_state_get_timeout_ms(void);
void screen_state_set_wrist_wake(bool enable);   ///< no-op without HAS_WRIST_TILT_SENSOR
bool screen_state_get_wrist_wake(void);
```

`set_timeout_ms(0)` must skip the comparison entirely, not compare against 0 and
sleep instantly.

**6.2 Watch faces: teardown, then a registry.** Neither face can currently be
removed -- both create a 1 Hz `lv_timer` and drop the handle
([`simple_face.cpp:136`](../watch_faces/simple_face.cpp#L136),
[`batman_dial.cpp:313`](../watch_faces/batman_dial.cpp#L313)), and
`batman_dial.cpp` keeps a file-static `lv_subject_t batt_subject` bound to two
widgets. So:

- each face stores its timer and gains `*_deinit()`, deleting the timer and its
  root container;
- `batman_dial_deinit()` may then re-`lv_subject_init_int()` on the next init
  safely: LVGL auto-unsubscribes an observer when the bound object is deleted
  (`unsubscribe_on_delete_cb`, registered in `lv_observer.c:327`), so deleting
  the widgets first leaves the subject with no live observers;
- `watch_faces/face_registry.{h,cpp}` holds the enum, display names, and
  `watch_face_apply(lv_obj_t *screen, WatchFaceId id)` which deinits the current
  face and inits the new one. `setupGui()` calls it with the persisted id
  instead of the hardcoded `batman_dial_init()` at `app_setup.cpp:92`.

**6.3 Notification popups get a duration.** `maybeShowMessagePopup()` builds an
`lv_msgbox` with no timer, so popups sit until dismissed. Add a one-shot
`lv_timer` that closes the tracked box, cancelled if the user dismisses it
first; read the interval from the settings store. `NOTIFICATION_POPUP_MIN/MAX`
from `new_interface/app_config.h` (2 s / 15 s) are sensible bounds to copy.

**6.4 One gate for haptics.** `gb_app.cpp` calls `gb_platform::vibrate()` from
nine places with either `GB_HAPTIC_TAP` (messages) or `GB_HAPTIC_ALERT` (calls,
alarms, find-device). Route them through one local helper that checks the
matching setting, rather than adding an `if` at nine call sites. Note the
find-device buzz is arguably not a "notification" -- the plan puts it under
calls/alarms, since the whole point is to be findable.

**6.5 Brightness has one owner.** The tray and the settings page both drive
`qst_hal_set_brightness()`. The tray already re-reads the HAL in
`refreshContent()` on every open, so it self-corrects; the settings page reads
at build time and rebuilds on every open (see 8.4), so it does too. What must
*not* happen is a second cached copy in `app_settings` treated as authoritative:
the store persists the value and re-applies it at boot, and the HAL is the live
truth in between.

## 7. Persistence

`app_settings.h` is one packed struct in the shape of factory's
`user_setting_params_t`, with a version field so a layout change is detected
rather than misread:

```c
constexpr uint16_t APP_SETTINGS_VERSION = 1;

struct AppSettings {
    uint16_t version;            ///< APP_SETTINGS_VERSION; mismatch -> defaults
    uint8_t  brightness;         ///< qst_hal range, board dependent
    uint16_t screen_timeout_s;   ///< 0 = never sleep
    uint8_t  wrist_wake;
    uint8_t  watch_face;         ///< WatchFaceId
    uint16_t notif_popup_ms;
    uint8_t  vibrate_messages;
    uint8_t  vibrate_alerts;
};

const AppSettings &app_settings(void);      ///< the live copy
void app_settings_begin(void);              ///< load (or default) + apply to subsystems
void app_settings_mark_dirty(void);
void app_settings_flush(void);              ///< write iff dirty
void app_settings_restore_defaults(void);
```

Storage mirrors factory exactly: `Preferences` under a namespace on `ARDUINO`,
a size+version check on read, defaults written back on mismatch; on native,
plain defaults each run (factory's `#else` branch). Optionally the native branch
can read/write a file under the scratch dir so persistence itself is testable in
the emulator -- worth it, since that is the only place this code can be
exercised quickly.

**Live-apply, commit once.** Each control applies its effect immediately (that
is what makes a brightness slider usable) and calls `app_settings_mark_dirty()`;
nothing writes NVS per event -- a slider drag fires `LV_EVENT_VALUE_CHANGED`
continuously and would hammer the flash. The single flush hangs off the screen:

```c
lv_obj_add_event_cb(screen_settings, [](lv_event_t *) { app_settings_flush(); },
                    LV_EVENT_SCREEN_UNLOAD_START, NULL);
```

This is better than factory's commit-in-the-back-button, which loses the edit on
any other exit path. `LV_EVENT_SCREEN_UNLOAD_START` fires on the outgoing screen
from both `lv_screen_load()` (`lv_display.c:1061`) and `lv_screen_load_anim()`
(`lv_display.c:724`), so it also covers the one exit the user does not
initiate: the sleep/wake path, where `onScreenWake()` loads the watch face out
from under whatever was showing.

## 8. Gotchas

**8.1 The widgets are available on both builds.** `LV_USE_MENU`, `_SLIDER`,
`_SWITCH` are 1 in LilyGoLib's bundled `lv_conf.h` (hardware) and default to 1
in `lv_conf_internal.h` under the emulator's `LV_CONF_SKIP`. No new build flags.

**8.2 The new screen needs styling.** `usable_area_init()` only styles the
screen that was active when it ran. Like `screen_gadgetbridge` at
[`app_setup.cpp:97`](../app_setup.cpp#L97), the settings screen needs
`usable_area_style_screen()`, and the menu itself must be built inside
`usable_area_rect()` -- `lv_menu`'s header back button sits top-left, the worst
place on the Ultra's curve.

**8.3 The tray floats above everything.** It lives on `lv_layer_top()`, so
loading a screen from underneath it leaves it hanging over the new screen.
Close it first (5, above). Closing is animated (~220 ms), which is fine --
`quick_settings_tray_close()` starts the animation and `settings_screen_open()`
switches the screen underneath it.

**8.4 Rebuild the contents, keep the screen.** Create `screen_settings` once in
`setupGui()` (matching how `screen_home`/`screen_gadgetbridge` are handled), but
have `settings_screen_open()` clean and rebuild the menu each time so every
control shows current values -- brightness in particular can have been changed
from the tray since the last visit. Rebuilding ~50 objects is not a cost worth
optimising, but the 1 Hz System Info timer must be deleted on close or each
visit leaks one.

**8.5 No input groups.** Factory wraps every row in `lv_group_add_obj()` for the
Pager's encoder/keyboard. `custom_interface` targets the touch-only Ultra and
never creates a group, so those calls port out. If the app is ever built for the
Pager, they come back -- worth a comment at the port site rather than silent
omission.

**8.6 Restore-defaults must re-apply, not just rewrite.** Writing defaults to
NVS while the live brightness, timeout and watch face keep their old values
leaves the device disagreeing with its own settings page until reboot.
`app_settings_restore_defaults()` runs the same apply path as
`app_settings_begin()`, then the page rebuilds itself.

## 9. Step order

1. `app_config.h` -- defaults, ranges, row metrics.
2. `settings/app_settings.{h,cpp}` -- struct, store, `begin()`/`flush()`, native
   and Arduino branches. Testable before any UI exists.
3. `settings/settings_widgets.{h,cpp}` -- the port from `ui_tools.cpp`.
4. Enablement, in dependency order and each independently verifiable:
   6.1 `screen_state` setters, 6.4 haptics gate, 6.3 popup timer, 6.2 face
   registry + `*_deinit()`.
5. `settings/settings_screen.{h,cpp}` -- the menu, wired to 4.
6. `setupGui()`: `app_settings_begin()` before the faces are built; create
   `screen_settings`; `watch_face_apply()` in place of `batman_dial_init()`.
7. Tray gear + `quick_settings_tray_set_action()`.
8. Grid tile -- needs the `GbGridEntry` action change and the 2x3 grid from
   section 5, so it lands after the button-grid plan.

Steps 1-7 do not depend on the button-grid work; only step 8 does.

## 10. Testing

```bash
pio run -e emulator_watch_ultra -t exec    # 410x502, curved: the layout case
pio run -e emulator_twatchs3   -t exec     # 240x240: the cramped case
```

- every row reachable and legible on both panels; nothing under the bezel on the
  Ultra, including the menu's back button and the bottom-most row;
- brightness moves the panel live, and the tray shows the same value when opened
  straight afterwards (and vice versa);
- screen timeout: set 10 s and confirm the idle blank; set 0 and confirm it
  never blanks;
- watch face switch flips the home screen and, after several flips, the second
  hand still ticks once per second -- a leaked timer shows up as a face that
  updates twice a second or faster;
- notification popup honours the duration slider; vibrate switches silence the
  right class of event (emulator prints the haptic call, hardware buzzes);
- exit via back button, via the tray, and by letting the screen sleep -- then
  reopen and confirm all three kept the edit;
- on hardware only: reboot and confirm the values survived NVS;
- restore defaults resets the controls *and* the live brightness/timeout/face
  without a reboot.

## 11. Out of scope

- **Phone-side settings sync.** `new_interface` mirrors these settings to
  Gadgetbridge over protocol §5.14/§6.8 (`GB_CHANGE_SETTINGS`, and
  [`new_interface/plans/watch-settings-sync-protocol-plan.md`](../../new_interface/plans/watch-settings-sync-protocol-plan.md));
  `custom_interface`'s `gb_app` has no `GB_CHANGE_SETTINGS` at all. Adding it is
  a protocol change, not a UI one -- a natural follow-up once these settings
  exist locally, and `plan.md`'s "Controlling from phone" section wants it.
- **Charger/OTG controls** from factory's `create_subpage_otg()`: they need PMU
  accessors `custom_interface` does not have.
- **Devices-status probe page**: needs factory's `hw_get_device_online()` bitmask
  HAL.
- **Theme/colour settings.** Nothing in this app reads a theme at runtime yet.
