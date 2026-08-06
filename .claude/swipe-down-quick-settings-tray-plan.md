# Plan: Swipe-down quick-settings tray for the factory UI

## Goal

Pull down from the top of the launcher to reveal a quick-settings shade — time, date, battery,
brightness, volume, Wi-Fi state and a shortcut to Settings — then swipe up, tap the scrim, or tap
the grabber to dismiss it.

Scope decisions (confirmed before design):

- **Quick-settings shade**, not an app grid and not a notifications list.
- Opens from the **launcher/home screen only**, so it can't fight widgets inside apps.
- **Touch boards only** — T-Watch-S3 (240×240) and T-Watch-Ultra (502×410), gated on
  `USING_TOUCHPAD`. T-LoRa-Pager has no touchscreen and compiles the feature out.

Today there is no way to check battery or change brightness without opening the Settings app,
which takes over the whole screen and loses your place in the launcher.

## Current state (verified 2026-08-06)

- Everything lives on `lv_screen_active()`. There is **no screen switching** anywhere —
  `lv_screen_load` / `lv_scr_load` appear nowhere in `src/factory`.
- `setupGui()` (`ui_main.cpp:523`) builds `main_screen = lv_tileview_create(...)` (`:560`) with two
  tiles: `menu_panel` = launcher (`:566`) and an app host (`:567`). Line `:570` removes
  `LV_OBJ_FLAG_SCROLLABLE`, so **the tileview is not user-swipeable** — tiles change only via
  `lv_tileview_set_tile_by_index()` in `menu_show()` (`:94`) / `menu_hidden()` (`:103`). Tile
  coordinates are unsigned, so there is no "row −1" to put a tray into.
- **`lv_layer_top()` and `lv_layer_sys()` are used nowhere** in the tree — free real estate that
  renders above both `main_screen` and `clock_page`.
- **No gesture handling exists at all**: `LV_EVENT_GESTURE`, `lv_indev_get_gesture_dir` and
  `LV_OBJ_FLAG_GESTURE_BUBBLE` have zero hits. Nothing competes for gestures.
- `ui_poll_timer_callback` (`ui_main.cpp:452`, 1 Hz) swaps `main_screen` ↔ `clock_page` after
  `SCREEN_TIMEOUT` (10 s, `:48`) of inactivity, but **only when `get_enter_low_power_flag()` is
  true** (`:456`). That flag is the hook an overlay uses to stay on screen.
- `create_app()` (`ui_main.cpp:149`) builds each launcher icon; its `LV_EVENT_CLICKED` lambda
  (`:175-191`) is the only place the app-launch sequence exists, and it touches the file-static
  `app_g`.
- `exit_func_cb` in `app_t` is **never invoked anywhere** — each app cleans up in its own back
  handler. There is no central "app closing" hook.
- LVGL 9 throughout: `lvgl/lvgl @ ^9.4.0` on hardware (`platformio.ini:194`), `lvgl@9.2.2` on the
  emulator envs (`:291`). The `#if LVGL_VERSION_MAJOR == 9 / == 8` split is pervasive
  (`ui_define.h:92-107`), but the v8 path is already dead — `ui_main.cpp` calls v9-only APIs
  unguarded.
- The emulator envs define `ARDUINO_T_WATCH_S3_ULTRA` / `ARDUINO_T_WATCH_S3`
  (`platformio.ini:313`, `:324`), and the board-macro chain at `hal_interface.h:1516-1574` is
  **not** wrapped in `#ifdef ARDUINO`. So both emulator watch envs get `USING_TOUCHPAD`
  (`:1540`, `:1560`) and exercise the tray with the SDL mouse, while `emulator_lora_pager`
  compiles the stub path. **The whole feature is testable natively, with no hardware.**

## Traps found while designing (read before writing code)

These are the things that would otherwise cost an afternoon each.

1. **`ui_define.h:99` does `#define lv_point_t lv_point_precise_t`.** `lv_indev_get_point()` takes
   a real `lv_point_t*` (`int32_t x,y`), but `lv_point_precise_t` is `float x,y` when
   `LV_USE_FLOAT=1` — which the emulator sets (`platformio.ini:267`). Passing the wrong struct
   corrupts the read silently. `ui_main.cpp:789` already works around this with a bare
   `#undef lv_point_t`. **Any file calling `lv_indev_get_point()` must do the same.** Avoiding that
   call altogether (see the gesture design) sidesteps the trap.

2. **`LV_EVENT_GESTURE` goes to the object under the finger at press time**, and walks upward only
   while each object carries `LV_OBJ_FLAG_GESTURE_BUBBLE`. A swipe starting on an app icon lands on
   the `lv_btn`, not on `menu_panel`. Two options, both fine:
   - Attach the callback to `menu_panel` and set `GESTURE_BUBBLE` on the icon buttons (in
     `create_app()`) and on the flex-row `panel` (`ui_main.cpp:573`) — two flags, and the launcher
     tile scopes the gesture to the home screen by construction.
   - Or attach to the **indev** with `lv_indev_add_event_cb()`, which LVGL dispatches
     unconditionally regardless of which object was hit — no flags at all, but then the
     launcher-only and modal guards must be written explicitly.

3. **`lv_layer_top()` is already non-clickable** (LVGL clears `LV_OBJ_FLAG_CLICKABLE` on it at
   display creation), **but hit-testing still recurses into its children**, and layer_top is
   searched *before* the active screen. So a clickable tray child would swallow launcher taps even
   with the tray "closed". Fix: keep `LV_OBJ_FLAG_HIDDEN` on the tray and scrim whenever closed —
   hit-testing returns immediately for hidden objects, and it skips rendering too. Never add
   `LV_OBJ_FLAG_CLICKABLE` to `lv_layer_top()` itself.

4. **`lv_indev_wait_release()` is mandatory, not a nicety.** Without it the swipe that opens the
   tray still delivers `LV_EVENT_CLICKED` to the button it started on, launching an app *behind*
   the tray. Call it right after `tray_open()` / `tray_close()`.

5. **`hw_set_user_setting()` writes NVS unconditionally** (`hal_interface.cpp:732`). Calling it from
   a slider's `LV_EVENT_VALUE_CHANGED` would mean one flash write per pixel of drag. `ui_sys.cpp`
   deliberately defers it to a single call on exit (`:61`) — do the same: set a dirty flag, flush
   once in `tray_close()`.

6. **The icon panel's horizontal scroll does *not* eat a vertical swipe.** LVGL picks the scroll
   axis from the dominant drag direction and won't select a horizontal-only object for a downward
   drag, so `scroll_obj` stays `NULL` and gesture detection proceeds.

7. **Keep the tray panel non-scrollable.** If it were vertically scrollable, LVGL would suppress
   the swipe-up-to-close gesture entirely (gesture detection bails when a scroll object is active).
   Size the content to fit instead.

8. **Fonts must already be enabled in the board's `lv_conf.h`.** `lv_font_montserrat_24` is used
   unguarded in `ui_power.cpp`, and each board defines `MAIN_FONT` (`hal_interface.h:1532`, `:1557`,
   `:1570`). Prefer those two. `lv_font_montserrat_32` is **not** referenced anywhere in
   `src/factory` today — don't be the first to assume it's compiled in.

9. **Use logical resolution for geometry.** Existing code branches on
   `lv_display_get_physical_*_resolution()` for font/style choices only. For positioning on
   `lv_layer_top()`, use `lv_display_get_vertical_resolution()` so a rotated display doesn't
   misplace the panel.

10. **`create_radius_button()` (`ui_tools.cpp:306`) sets `LV_OBJ_FLAG_FLOATING`**, which removes the
    button from flex layout. Build the tray's action buttons inline with `lv_btn_create`.

11. **`create_slider()` / `create_switch()` wrap `lv_menu_cont_create()`** (`ui_tools.cpp:191`).
    That works as a plain flex child (no `lv_menu` ancestor required), but it carries menu styling.
    Either accept it for consistency with the Settings app, or build sliders directly.

## Step 1 — `ui_define.h`

Add unconditional declarations, so no caller ever needs an `#if`:

```c
extern lv_obj_t *menu_panel;          /* defined ui_main.cpp:51, currently not exported */
bool get_enter_low_power_flag();      /* defined ui_main.cpp:88, currently declared nowhere */
void ui_launch_app(app_t *app);       /* new helper, Step 2 */

/* Swipe-down quick settings tray. No-ops on boards without a touchpad. */
void tray_init();
void tray_open();
void tray_close();
bool tray_is_open();
```

Also move `#define SCREEN_TIMEOUT 10000` here from `ui_main.cpp:48`, so the tray's idle auto-close
(Step 5) uses the same constant rather than a duplicate.

## Step 2 — `ui_main.cpp`

**Extract the app-launch sequence** out of the `LV_EVENT_CLICKED` lambda (`:175-191`) so the tray's
Settings shortcut can reuse it instead of duplicating it:

```c
void ui_launch_app(app_t *app)
{
    if (!app || lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) return;
    set_default_group(app_g);
    hw_feedback();
    if (app->setup_func_cb) (*app->setup_func_cb)(lv_obj_get_child(main_screen, 1));
    menu_hidden();
}
```

Then the lambda body becomes `if (c == LV_EVENT_CLICKED) ui_launch_app(func_cb);`.

**Enable gesture bubbling** (if using the `menu_panel` approach from trap 2): add
`lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);` in `create_app()` and the same on `panel`
after `:573`.

**Call `tray_init();`** between `:703` and `:705` — after `setupClock()` so the tray is built last,
and before `disp_timer` is created so the tray's refresh timer sits ahead of the poll timer in
LVGL's timer list (which removes a one-tick race where the tray closes and the clock face appears
in the same frame).

**Optional guard:** `hw_device_poll()` (`ui_main.cpp:426`) does `lv_obj_clean(lv_screen_active())`
on critical battery, which cleans the active screen only — an open tray would float above the
"Battery Low! Shutting down..." message. One line at the top of that branch fixes it.

## Step 3 — new `src/factory/ui_tray.cpp`

Whole file wrapped in `#if defined(USING_TOUCHPAD)` with `#else` no-op stubs. `ui_define.h`
includes `hal_interface.h`, so the macro is defined by the time the guard is evaluated.
`src_dir = src/factory` globs all `.cpp`, so **no `platformio.ini` change is needed**.

Object tree, all on `lv_layer_top()`:

```
lv_layer_top()
├── scrim   100%x100%, black LV_OPA_50, CLICKABLE, HIDDEN when closed → tray_close()
└── tray    100% x tray_h, y animated from -tray_h, HIDDEN when closed,
            opaque bg (the dark theme is translucent in places), flex column
    ├── header    time + date  |  battery bar + % + Wi-Fi chip
    ├── brightness row  icon + slider + % label
    ├── volume row      icon + slider
    └── footer    Settings shortcut | grabber (LV_SYMBOL_UP) → tray_close()
```

**Group hygiene:** LVGL adds focusable widgets to the *default* group as they are built. Wrap
construction in `lv_group_set_default(NULL)` / restore, so tray sliders never land in `menu_g` and
pollute launcher encoder navigation. The tray is touch-only and needs no group of its own. (If it
is ever extended to the T-LoRa-Pager, that is when to copy the `prev_group`/`msg_group`
save-restore from `create_msgbox()` / `destroy_msgbox()`, `ui_tools.cpp:127` / `:114`.)

**Animation:** `lv_anim_t` on `y`, ~200 ms, `lv_anim_path_ease_out` opening / `ease_in` closing,
with `lv_anim_set_completed_cb` on the close animation hiding both objects. Use a named
`static void tray_anim_y_cb(void *var, int32_t v)` wrapper rather than casting `lv_obj_set_y` —
the cast warns under `-Wcast-function-type` in C++. Track state with an enum
(`CLOSED / OPENING / OPEN / CLOSING`), not a bool, so a gesture mid-animation can't double-fire.

**Per-board sizing** (branch with `is_screen_small()`, already declared at `hal_interface.h:1472`;
it `printf`s on every call, so call it once and cache):

| | 240×240 | 502×410 |
|---|---|---|
| tray height | full screen | ~70% |
| padding / corner radius | 6 / 0 | 16 / 24 |
| clock font | `MAIN_FONT` | `lv_font_montserrat_24` |

On the 240×240 the tray is effectively a full-screen sheet and the scrim is never visible — keep
the scrim anyway so open/close logic is identical on both boards, and rely on the grabber there.

**Content wiring:**

| Element | Source |
|---|---|
| Time + date | `hw_get_date_time(struct tm&)` (`hal_interface.h:384`); copy the `week[]` table and the `tm_wday > 6 ? 6 : tm_wday` clamp from `clock_update_datetime` (`ui_main.cpp:220-227`) — the RTC can return garbage |
| Battery bar + % | `hw_get_monitor_params(p)` → `p.battery_percent` (`hal_interface.h:682`, struct `:225`) |
| Brightness slider | range `hw_get_disp_min_brightness()`..`hw_get_disp_max_brightness()` (`:976`/`:985`); on change → `hw_set_disp_backlight(val)` + set dirty flag. Mirrors `display_brightness_cb` (`ui_sys.cpp:71`). `map_r()` is file-static in `ui_sys.cpp:41` — duplicate the 8 lines privately rather than de-static'ing it; it clamps out-of-range input |
| Volume slider | `hw_get_volume()` / `hw_set_volume()` (`:616`/`:610`) |
| Wi-Fi chip | `LV_SYMBOL_WIFI` + `hw_get_wifi_connected()` / `hw_get_wifi_rssi()` (`:526`/`:405`) |
| Settings shortcut | `tray_close();` then `ui_launch_app(&ui_sys_main);` — close **first**, so the low-power restore runs before the app takes over |

**Re-sync on open** from `hw_get_user_setting()` (`hal_interface.h:830`) rather than
`hw_get_disp_backlight()`, so the tray reflects changes made in the Settings app. (Natively
`hw_get_disp_backlight()` returns a hardcoded `100`.)

**Refresh timer:** follow the `clock_timer` pattern (`ui_main.cpp:365`) — create once in
`tray_init()`, immediately `lv_timer_pause()`, then resume/pause on open/close rather than
create/delete. Call `lv_timer_ready()` on open so the tray never animates in showing `--:--`.

## Step 4 — Gesture handling

Open on `LV_DIR_BOTTOM`, close on `LV_DIR_TOP`, reading
`lv_indev_get_gesture_dir(lv_indev_active())` — the v9 spelling; `lv_indev_get_act()` is v8 and
does not exist here.

Guards needed before opening:

- `lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)` → the clock face is up; the touch should just
  wake the device.
- Launcher tile only — implicit if the callback is on `menu_panel`, otherwise
  `lv_tileview_get_tile_active(main_screen) != menu_panel`.
- If using the indev-level callback, also reject gestures made over a modal:
  `lv_obj_get_screen(gobj) != lv_screen_active()`. `create_msgbox()` calls `lv_msgbox_create(NULL)`
  (`ui_tools.cpp:142`), which parents the msgbox to layer_top, so this one comparison covers both
  msgboxes and the tray itself.

Then `tray_open(); lv_indev_wait_release(indev);`.

Note LVGL's `gesture_min_velocity` is 3 px/sample — a very slow "pull down" never registers as a
gesture. Inherent to LVGL; worth knowing when testing.

## Step 5 — Idle / low-power interaction

Three separate concerns:

1. **Stay on screen.** `set_low_power_mode_flag(false)` on open, restore on close. **Save and
   restore the previous value** rather than hardcoding `true` on close — `ui_msg_pop_up()`
   (`ui_msg.cpp:19`) hardcodes it, which is a latent bug if two overlays ever nest. This is why
   `get_enter_low_power_flag()` needs a declaration (Step 1).
2. **Reset the inactivity clock.** `lv_display_trigger_activity(NULL)` in both `tray_open()` and
   `tray_close()` — the latter so closing doesn't immediately drop to the clock face because the
   countdown ran the whole time the tray was up. **Not** from the refresh timer, which would keep
   the screen awake forever.
3. **Auto-close when idle.** Because (1) blocks the clock transition, a tray left open holds the
   screen on at 240 MHz indefinitely. In the 1 Hz refresh callback:
   `if (lv_display_get_inactive_time(NULL) > SCREEN_TIMEOUT) { tray_close(); return; }`. The next
   poll tick then finds the flag restored and takes the normal idle path — much cleaner than
   teaching `ui_poll_timer_callback` about the tray.

The display-blanking branch (`ui_main.cpp:485-520`) is already unreachable while the tray is open,
since it is gated on `main_screen` being hidden, which can no longer happen.

## Verification

**Native first — it is the only visual check without hardware.** The toolchain is not preinstalled
in a fresh container (`pip install platformio`, `apt-get install libsdl2-dev`); `.devcontainer/`
ships both.

```bash
pio run -e emulator_watch_ultra -t exec    # 502x410, USING_TOUCHPAD defined
pio run -e emulator_twatchs3   -t exec     # 240x240, USING_TOUCHPAD defined
pio run -e emulator_lora_pager             # compile-only: proves the stub path builds
pio run -e tlora_pager                     # the real exclusion check
```

The SDL mouse is a `LV_INDEV_TYPE_POINTER`, so press-and-drag down is a genuine gesture. Requires
X11/`DISPLAY` forwarding — commented out in `.devcontainer/devcontainer.json`.

Emulator behaviours that are **not** bugs: battery percent jitters every second
(`hal_interface.cpp:1578` returns `30 + rand() % 71` per call); the brightness slider moves but
nothing dims (`hw_set_disp_backlight` is `#ifdef ARDUINO`-only); `hw_feedback()` is a no-op;
`hw_set_user_setting()` doesn't persist. The idle→clock transition **does** run natively, so the
Step 5 logic is genuinely testable.

**Test matrix** (per touch board):

| # | Action | Expected |
|---|---|---|
| 1 | Swipe down on launcher | Tray slides in, haptic tick, scrim dims the launcher |
| 2 | Swipe down starting **on an app icon** | Tray opens; **no app launches** |
| 3 | Swipe horizontally across icons | Icons scroll and snap as before; tray does not open |
| 4 | Open an app, swipe down inside it | Nothing |
| 5 | On the clock face, swipe down | Wakes to launcher only; tray does not open |
| 6 | Open tray, wait > 10 s | Tray auto-closes, then the clock face appears normally |
| 7 | Swipe up / tap scrim / tap grabber | Each closes the tray |
| 8 | Drag brightness slider | Backlight tracks live; no per-pixel NVS chatter on serial |
| 9 | Close tray, reboot | Brightness persisted (one `set brightness_level:` line per close) |
| 10 | Change brightness in Settings, reopen tray | Tray slider shows the Settings value |
| 11 | Tray closed → tap each launcher icon | Every app still opens; nothing swallowed by layer_top |
| 12 | Open tray, tap Settings shortcut | Tray closes, Settings opens, back returns to launcher |
| 13 | Trigger a msgbox, swipe down over it | Tray does not open |

Test 11 catches the `lv_layer_top()` hit-testing trap. Test 2 catches a missing
`lv_indev_wait_release()`. Test 6 catches a broken low-power save/restore.

## Suggested build order

1. `ui_define.h` declarations + `SCREEN_TIMEOUT` move.
2. `ui_main.cpp`: `ui_launch_app()`, gesture-bubble flags, `tray_init()` call site.
3. `ui_tray.cpp` skeleton with the `#if` / `#else` stubs — **build `emulator_lora_pager` now**, the
   cheapest check that the stub path and header changes are sound.
4. Object tree + sizing, hidden-when-closed. Wire `tray_open()` to a debug trigger and **verify
   test 11 before adding any content**.
5. Animations, state enum, the three close paths.
6. Gesture callback. Verify tests 1-5, 7.
7. Time / battery / refresh timer + idle auto-close. Verify test 6.
8. Brightness and volume sliders, dirty-flag persistence, re-sync on open. Verify tests 8-10.
9. Settings shortcut, low-battery guard. Verify test 12.
10. Full matrix on both emulator watch envs, then both physical watches, then
    `pio run -e tlora_pager`.

## Open risks

- **Two LVGL versions.** Emulator pins 9.2.2, hardware uses ^9.4.0. Every API above exists in both,
  but avoid the v8 spellings: `lv_anim_set_time`, `lv_anim_set_ready_cb`, `lv_anim_del`,
  `lv_obj_clear_flag`, `lv_indev_get_act`.
- **`exit_func_cb` is dead code**, so there is no central hook for "an app is closing". If the tray
  ever needs to react to that, it has to go through each app's own back handler.
- **T-LoRa-Pager is excluded, not supported.** If it is ever wanted there, the tray needs an
  encoder/keypad trigger and its own indev group — a materially different design, not a tweak.
