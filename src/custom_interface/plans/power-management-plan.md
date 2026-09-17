# Plan: power management for custom_interface

Four asks: hold the power button to shut down (and it turns back on the same
way), a Power page in Settings, a power control in the slide-down tray, and a
dedicated Sleep action. `custom_interface` targets the T-Watch-Ultra only (PMU
board, no PPM branch needed -- see `app_config.h`'s header), so everything
below is written against `instance.pmu`/`instance.sleep()` directly, no
board `#ifdef` required.

## 1. What already works, and what's missing

| Behaviour | State today |
|---|---|
| Single click wakes/sleeps the **display** | Already works -- `screen_state.cpp`'s `powerButtonEventCb()` (`screen_state.cpp:93-98`) sets a flag on `PMU_EVENT_KEY_CLICKED`, and `manageSleepState()` (`screen_state.cpp:245-256`) toggles `instance.sleepDisplay()`/`wakeupDisplay()` on it. This is *not* deep sleep -- RAM, BLE and app state are untouched. |
| Power button wakes a sleeping/off device | Hardware-level, nothing to write. The PMU chip stays powered when the ESP32 is off or asleep and re-powers/resets it on a button press; firmware only runs again from `setup()`. |
| Hold power button to shut down | **Missing.** `PMU_EVENT_KEY_LONG_PRESSED` exists (confirmed in `LilyGoEventManage.h` and dispatched by `LilyGoWatchUltra.cpp:557-560`, `pmu.isPekeyLongPressIrq()`) but nothing in `custom_interface` listens for it. |
| A menu/tray entry for power actions | **Missing.** No `hal_interface.cpp`-style module exists here for it (unlike `src/factory`), and no UI surfaces it. |
| Deep-sleep "Sleep" action | **Missing**, and distinct from the display-sleep above -- see 1.1. |

### 1.1 Two different things both called "sleep"

This codebase (and factory) actually has three power states, and the ask's
"Sleep" button should be the middle one, matching `src/factory/ui_power.cpp`'s
own "Sleep" menu item exactly:

1. **Display off** -- `instance.sleepDisplay()`. Already wired to a single
   power-button click and the idle timeout. Instant resume, nothing lost.
2. **Deep sleep** -- `instance.sleep()`. ESP32 deep sleep: RAM is lost, the
   BLE link drops, and waking re-runs `setup()` from scratch. This is what
   factory's `hw_sleep()` calls (`src/factory/hal_interface.cpp:1810-1826`)
   and what its Power app's "Sleep" button triggers.
3. **Shutdown** -- `instance.pmu.shutdown()`. Drops the power rails entirely;
   only a button press or charger insertion brings the rails back, at which
   point the ESP32 boots cold. Factory's `hw_shutdown()`
   (`hal_interface.cpp:1788-1798`).

The plan below builds (2) as "Sleep" and (3) as the hold-to-power-off action,
matching factory's precedent rather than inventing new semantics.

### 1.2 Power-key wake is already armed for free

`LilyGoWatchUltra.h:445-446` defaults `instance.sleep()`'s wake source to
`WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_BOOT_BUTTON`. So calling `instance.sleep()`
with no arguments already satisfies "power button also wakes from sleep" --
don't override `wakeup_src`.

## 2. New primitives in `screen_state`

`screen_state.cpp` is already the one file that owns the power-button PMU
event and talks to `instance` for sleep/wake, so the new actions belong here
too rather than a new module. Add to `screen_state.h`:

```c
/*Deep sleep: instance.sleep() -- RAM lost, BLE drops, wakes via the power
  button or BOOT button and re-runs setup(). A no-op (with a log line) on the
  emulator, which has no PMU/deep-sleep to enter.*/
void screen_state_sleep(void);

/*Full power off: fades the backlight, then instance.pmu.shutdown() -- drops
  the rails, never returns. A no-op (with a log line) on the emulator.*/
void screen_state_shutdown(void);

/*Fires when the power button is HELD (PMU_EVENT_KEY_LONG_PRESSED) while the
  display is already awake -- a long press landing on a dark screen only
  wakes it, same as a click, so this never fires from asleep. Screen
  navigation/confirmation lives outside this module (same reasoning as
  ScreenWakeCallback above), so this is a hook, not an action.*/
typedef void (*ScreenLongPressCallback)(void);
void screen_state_set_long_press_cb(ScreenLongPressCallback cb);
```

In `screen_state.cpp` (ARDUINO branch):

- Add `static bool power_button_long_pressed = false;` and a
  `static ScreenLongPressCallback long_press_cb = NULL;` beside the existing
  `power_button_clicked`.
- In `powerButtonEventCb()`, add the second branch:
  ```c
  if (instance.getPMUEventType(params) == PMU_EVENT_KEY_LONG_PRESSED) {
      power_button_long_pressed = true;
  }
  ```
- In `manageSleepState()`, check the long-press flag *before* the existing
  click branch (`screen_state.cpp:245`), same shape as the click handling:
  ```c
  if (power_button_long_pressed) {
      power_button_long_pressed = false;
      if (screen_asleep) {
          // landed on a dark screen -- wake only, never shutdown-confirm
          // blind (same rule onBootButton() documents in app_setup.cpp)
          instance.wakeupDisplay();
          wake();
      } else if (long_press_cb) {
          long_press_cb();
      }
      last_activity_ms = millis();
  } else if (power_button_clicked) {
      ... // unchanged
  }
  ```
- `screen_state_sleep()`:
  ```c
  void screen_state_sleep(void)
  {
      instance.decrementBrightness(0, 5, false);   // fade, matches hw_sleep()
      instance.sleep();   // does not return; wakes via setup()
  }
  ```
- `screen_state_shutdown()`:
  ```c
  void screen_state_shutdown(void)
  {
      instance.decrementBrightness(0, 5, false);
      instance.pmu.shutdown();   // does not return
  }
  ```

Native/emulator branch: stub both with a `printf` (matching the existing
`screen_state_wake_display()` native stub at `screen_state.cpp:368-376`) --
there's no PMU or deep sleep on the host, so this only needs to prove the
call sites wire up correctly.

## 3. Wiring the long press: confirm, then shut down

`screen_state.cpp` deliberately knows nothing about screens (see its own
comment on `ScreenWakeCallback`), so the confirmation dialog and the actual
`screen_state_shutdown()` call go in `app_setup.cpp`, next to `onBootButton()`
and `onScreenWake()`:

```c
void onPowerButtonLongPress()
{
    // A held press is easy to trigger by accident (wrist against something,
    // pocket pressure) so this confirms rather than acting immediately --
    // same reasoning as settings_screen.cpp's restoreClicked()/restoreConfirmed().
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_obj_set_width(box, usable_area_screen_width() - 2 * SAFE_INSET);
    lv_msgbox_add_title(box, "Power off?");
    lv_msgbox_add_text(box, "The watch will need the power button or a "
                            "charger to turn back on.");
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(box, "Power Off"),
                        [](lv_event_t *) { screen_state_shutdown(); },
                        LV_EVENT_CLICKED, NULL);
    lv_msgbox_add_close_button(box);
}
```

Wire it in `setupGui()` next to `screen_state_set_wake_cb(onScreenWake)`
(`app_setup.cpp:226`):

```c
screen_state_set_long_press_cb(onPowerButtonLongPress);
```

`app_setup.cpp` already includes `<usable_area.h>` transitively via
`usable_area_init()`'s header; confirm `usable_area_screen_width()`/
`SAFE_INSET` are visible (they are, via `<usable_area.h>`, already used the
same way in `settings_screen.cpp:222`).

## 4. Settings: a "Power" subpage

Add to `settings_screen.cpp`'s `buildMenu()` (`settings_screen.cpp:369-373`),
alongside the other `addSubpage()` calls:

```c
buildPowerPage(addSubpage(main_page, LV_SYMBOL_POWER, "Power"));
```

with:

```c
void sleepClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    screen_state_sleep();
}

void shutdownConfirmed(lv_event_t *e)
{
    lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent((lv_obj_t *)lv_event_get_current_target(e))));
    screen_state_shutdown();
}

void shutdownClicked(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_obj_set_width(box, usable_area_screen_width() - 2 * SAFE_INSET);
    lv_msgbox_add_title(box, "Power off?");
    lv_msgbox_add_text(box, "The watch will need the power button or a "
                            "charger to turn back on.");
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(box, "Power Off"),
                        shutdownConfirmed, LV_EVENT_CLICKED, NULL);
    lv_msgbox_add_close_button(box);
}

void buildPowerPage(lv_obj_t *page)
{
    settings_button(page, LV_SYMBOL_EYE_CLOSE, "Sleep",
                    "Sleep", sleepClicked, NULL);
    settings_button(page, LV_SYMBOL_POWER, "Power off",
                    "Power Off", shutdownClicked, NULL);
}
```

This is the exact `settings_button()` + confirm-`lv_msgbox` shape
`restoreClicked()`/`restoreConfirmed()` already use
(`settings_screen.cpp:205-230`) -- the shutdown confirm text and flow above are
close enough to factor `shutdownClicked`'s msgbox into one shared helper with
`app_setup.cpp`'s `onPowerButtonLongPress()` if you'd rather not keep two
copies; both are small enough that duplicating is also fine.

Needs `#include "../screen_state/screen_state.h"` added to
`settings_screen.cpp`'s includes (not currently there).

## 5. Quick-settings tray: a Sleep button

The ask calls out the tray specifically for **Sleep**, not Shutdown -- keep
the destructive action out of a one-tap swipe-down surface; it already lives
behind Settings (deliberate menu) and the held button (deliberate hold +
confirm). Add a plain button (not a checkable toggle -- `makeToggle()`
persists checked state, which a momentary action must not have) to the
footer band in `quick_settings_tray.cpp`, next to the grabber
(`quick_settings_tray.cpp:292-320`):

```c
void sleepClickCb(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_settings_tray_close();   // closing before a screen/state change --
                                    // same rule the gear button follows
    screen_state_sleep();
}
```

and in `buildFooter()`, a small icon button mirroring how `s_settings_button`
is built (`quick_settings_tray.cpp:310-319`) but on the *left* side of the
band so it doesn't collide with the gear:

```c
lv_obj_t *s_sleep_button = lv_label_create(band);
lv_obj_add_flag(s_sleep_button, LV_OBJ_FLAG_IGNORE_LAYOUT);
lv_obj_set_style_text_color(s_sleep_button, lv_palette_main(LV_PALETTE_GREY), 0);
lv_obj_set_style_text_font(s_sleep_button, APP_FONT_QST_ICON, 0);
lv_label_set_text(s_sleep_button, LV_SYMBOL_EYE_CLOSE);
lv_obj_align(s_sleep_button, LV_ALIGN_LEFT_MID, APP_QST_GEAR_PAD_RIGHT, 0);
lv_obj_set_ext_click_area(s_sleep_button, APP_QST_GEAR_EXT_CLICK);
lv_obj_add_flag(s_sleep_button, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_event_cb(s_sleep_button, sleepClickCb, LV_EVENT_CLICKED, NULL);
```

Needs `#include "../screen_state/screen_state.h"` added to
`quick_settings_tray.cpp`'s includes. Unlike the gear
(`quick_settings_tray_set_action()`), this button is always shown -- sleep is
a capability of the tray itself, not something the caller opts into, so no
new `Qst*` indirection is needed.

## 6. Step order

1. `screen_state.{h,cpp}` -- `screen_state_sleep()`, `screen_state_shutdown()`,
   long-press detection + `screen_state_set_long_press_cb()`. Testable alone:
   confirm the emulator prints the right stub line for each on the B key
   path... actually the emulator has no PMU events at all (see 7 below), so
   this step is verified on hardware, or by calling the new functions from a
   temporary button during development.
2. `app_setup.cpp` -- `onPowerButtonLongPress()` + wiring.
3. `settings_screen.cpp` -- Power subpage.
4. `quick_settings_tray.cpp` -- Sleep button.

Steps 3 and 4 depend only on step 1's new functions, not on each other or on
step 2.

## 7. Gotchas

**7.1 The emulator can't exercise the power button at all.** Unlike
`boot_button.cpp`, which maps GPIO 0 to the B key for the native build,
`screen_state.cpp`'s native branch has no PMU stand-in (its own comment says
so: "no PMU (power button) ... on the host", `screen_state.cpp:317`). So
`screen_state_sleep()`/`screen_state_shutdown()` and the long-press path are
only real on `pio run -e twatch_ultra`. Stub them with a `printf` so the
Settings-page and tray-button call sites still build and can be tapped in the
emulator (confirming navigation/msgbox flow), even though the underlying
action is inert there -- exactly how `hw_shutdown()`/`hw_sleep()` do it in
factory.

**7.2 Deep sleep drops the BLE link.** `screen_state_sleep()` resets the ESP32;
Gadgetbridge will see the watch disconnect and it re-advertises fresh from
`setup()`. That's expected (factory's Sleep does the same), but worth
mentioning in the confirm text if you want to add one to the Sleep buttons --
the plan above leaves Sleep un-confirmed since it's non-destructive and
matches the single click's directness, but add a msgbox there too if a silent
BLE drop would surprise testers.

**7.3 No audio/mic teardown needed here.** Factory's `hw_sleep()` explicitly
kills a player task and ends the mic/PCM peripherals before sleeping
(`hal_interface.cpp:1803-1821`) because factory has an audio app holding those
peripherals open. `custom_interface` has no audio app, so
`screen_state_sleep()` doesn't need that step -- don't port it reflexively.

**7.4 `instance.pmu.shutdown()` needs `USING_PMU_MANAGE`.** It's already
implied by every other `instance.pmu.*` call already in this tree
(`gb_platform.cpp`, `quick_settings_tray_hal.cpp`, `simple_face.cpp`), so no
new build flag is needed for the Ultra env -- just confirming this isn't a
new dependency.

**7.5 The PMU's own hard cutoff is separate and out of scope.** Most PEK-key
PMICs (AXP2101 included) also have their own multi-second hardware
force-power-off timer, independent of any firmware IRQ. `PMU_EVENT_KEY_LONG_PRESSED`
here is a *shorter*, firmware-handled press (LilyGoLib just forwards the IRQ,
confirmed at `LilyGoWatchUltra.cpp:557-560` -- it does nothing else on it), so
the two don't fight; nothing to configure for it.

**7.6 `plan.md` says "double press" for the power menu.** The top-level
`plan.md` backlog item reads "add a double press of the power button to bring
up power menu," which is a different gesture from this plan's hold-to-confirm
design. This plan follows your literal ask (hold = power off); update or
strike that `plan.md` line once this ships so the two don't disagree.

## 8. Testing

```bash
pio run -e emulator_watch_ultra -t exec
```
- Settings > Power: both rows present, Sleep runs with no confirm (prints the
  stub line), Power Off shows the confirm dialog and Cancel actually cancels.
- Tray: swipe down, tap the new Sleep icon, confirm the tray closes first and
  the stub line prints.

```bash
pio run -e twatch_ultra -t upload && pio run -e twatch_ultra -t monitor
```
- Single click: display sleeps/wakes as before (regression check -- this
  plan must not touch that path's behavior).
- Hold past a short press: confirm dialog appears (only when the screen was
  already awake); Power Off actually cuts power; pressing the button again
  brings it back and boots clean.
- From Settings > Power, Sleep: device deep-sleeps, and a power-button press
  wakes it (confirms the default `WAKEUP_SRC_POWER_KEY` wiring from 1.2)
  rather than needing the BOOT button.
- From the tray, Sleep: same deep-sleep behavior, reachable without opening
  Settings.
- Gadgetbridge/phone: confirm reconnect after both Sleep and Power Off wake
  paths -- expect a fresh advertise, not a resumed session.

## 9. Out of scope

- **Charger/OTG controls** (factory's `create_subpage_otg()`) -- already
  flagged out of scope in `plans/settings-page.md` for lacking a PMU HAL here;
  unrelated to this plan's asks.
- **A "confirm" dialog on Sleep** -- left un-confirmed per 7.2's reasoning;
  add one later if testing shows the BLE drop surprises people.
- **Reconciling `plan.md`'s "double press" note** -- flagged in 7.6, not
  resolved here since it's a product decision, not an implementation detail.
