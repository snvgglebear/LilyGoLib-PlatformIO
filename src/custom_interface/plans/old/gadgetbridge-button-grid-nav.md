# Plan: button-grid launcher for the Gadgetbridge screen

Replace the Gadgetbridge screen's bottom tab bar with a 2x2 grid of large
buttons -- one per existing tab -- and keep left/right swiping as the way to
move between tabs once one is open.

Nothing about `GbApp`, `gb_messages`, the protocol layer or the modal overlays
changes. This is a navigation-chrome change confined to `gb_ui.cpp`,
`gb_ui_metrics.h`, `gb_ui.h` and `app_setup.cpp`, whose screen-level gesture
handler needs a guard (section 4.7) so that scrolling a list or swiping between
tabs is no longer also read as a screen navigation gesture -- required here,
and a fix for a bug the app has today.

## 1. Where things stand

`gb_ui_begin()` (`gadgetbridge_ble/gb_ui.cpp:957`) builds, inside one
`usable_area_rect()`:

```
+-------------------------------+
| status bar   BT ...    BAT 82%|   30px (20px on S3)
+-------------------------------+
|                               |
|        active tab page        |   flex-grow 1
|                               |
+-------------------------------+
| Watch | Chats | Alerts | Music|   48px (34px on S3)  <- lv_tabview's tab bar
+-------------------------------+
```

Four pages come from `lv_tabview_add_tab()` at `gb_ui.cpp:977-980`, each built
by its own `build*Tab()` function. The four labels of the tab bar are the only
way to switch pages today.

## 2. Target design

The grid becomes **page 0 of the same tabview**, the tab bar is hidden, and a
small home button in the status bar returns to the grid from any page:

```
lv_tabview  (tab bar hidden, size 0)
 |
 +-- tab bar   (hidden: LV_OBJ_FLAG_HIDDEN, skipped by the flex layout)
 +-- content   (snap-scrolling row -- this is what left/right swipe drives)
      +-- [0] Grid     2x2 buttons: Watch / Chats / Alerts / Music
      +-- [1] Watch    (unchanged buildWatchTab)
      +-- [2] Chats    (unchanged buildChatsTab)
      +-- [3] Alerts   (unchanged buildAlertsTab)
      +-- [4] Music    (unchanged buildMusicTab)
```

```
+-------------------------------+      +-------------------------------+
|[=]  BT Advertising    BAT 82% |      |[=]  BT Advertising    BAT 82% |
+-------------------------------+      +-------------------------------+
|  +-----------+ +-----------+  |      |                               |
|  |    (o)    | |    (@)    |  | tap  |            10:24              |
|  |   Watch   | |   Chats   |  | ---> |         Tue 19 Aug            |
|  +-----------+ +-----------+  |      |        18C  Cloudy            |
|  +-----------+ +-----------+  | <--- |                               |
|  |    (!)    | |    (>)    |  | [=]  |    [ Ring my phone ]          |
|  |  Alerts   | |   Music   |  |      |                               |
|  +-----------+ +-----------+  |      |   <- swipe -> cycles pages    |
+-------------------------------+      +-------------------------------+
        page 0 (grid)                          page 1 (Watch)
```

Three decisions behind this shape:

- **Grid inside the tabview, not beside it.** Swiping is already implemented by
  `lv_tabview`'s content container; putting the grid in that container means
  the grid participates in the same swipe chain for free and there is one
  widget tree, one active-page notion, and no show/hide bookkeeping.
- **Home button in the status bar,** not a swipe. Left/right is spent on tab
  navigation, and up/down are already taken on this screen -- `app_setup.cpp`'s
  `onGadgetbridgeGesture()` maps swipe-up to the watch face and swipe-down to
  the quick-settings tray. A visible button is also the only affordance that
  gets the user back from Music in one tap instead of three swipes.
- **Tab bar removed entirely.** That reclaims 48px on the Ultra / 34px on the
  S3 for the Alerts and Chats lists, which are the two screens that actually
  run out of room.

## 3. Why left/right swipe needs no new code

Worth stating explicitly, because it determines how small this change is:
`lv_tabview`'s content container is *already* a horizontally snap-scrolling
container, and it already reacts to a drag. From
`.pio/libdeps/*/lvgl/src/widgets/tabview/lv_tabview.c`:

- the constructor gives the content `LV_OBJ_FLAG_SCROLL_ONE` and turns the
  scrollbar off (`lv_tabview_constructor`);
- `lv_tabview_set_tab_bar_position()` sets `lv_obj_set_scroll_snap_x(cont,
  LV_SCROLL_SNAP_CENTER)` for a top/bottom bar;
- `cont_scroll_end_event_cb()` converts wherever the drag landed into a tab
  index, calls `lv_tabview_set_active()` and sends `LV_EVENT_VALUE_CHANGED`.

So swipe-to-change-tab works in this app *today* -- it is just undiscoverable
next to a tab bar. Two consequences to lean on:

- Hiding the tab bar does not remove swipe navigation; the two are independent.
- A horizontal drag that starts on an `lv_list` row (Chats/Alerts) still
  changes tab: the list has no horizontal overflow, so LVGL's scroll search
  (`find_scroll_obj` in `lv_indev_scroll.c`) walks past it to the tabview
  content, which does. Vertical drags still scroll the list. No per-widget
  scroll-direction tweaks needed.

## 4. Changes, file by file

### 4.1 `gb_ui_metrics.h`

Delete the two now-unused tab-bar constants and add the grid/status metrics:

```c
/// Status bar height, by panel size. Sized to hold the home button, not just
/// the two labels -- see GB_STATUS_BUTTON_SIZE.
constexpr int32_t GB_STATUS_BAR_HEIGHT_LARGE = 40;   ///< was 30, inline in buildStatusBar()
constexpr int32_t GB_STATUS_BAR_HEIGHT_SMALL = 24;   ///< was 20

/// Square side of the status bar's back-to-grid button. Smaller than
/// GB_BUTTON_HEIGHT because it shares a strip with the link/battery labels;
/// lv_obj_set_ext_click_area() below makes the *tappable* area finger-sized.
constexpr int32_t GB_STATUS_BUTTON_SIZE_LARGE = 34;
constexpr int32_t GB_STATUS_BUTTON_SIZE_SMALL = 22;
constexpr int32_t GB_STATUS_BUTTON_EXT_CLICK  = 10;  ///< invisible tap margin

/// Launcher grid: two columns, and the gap/padding around its tiles.
constexpr int32_t GB_GRID_COLS    = 2;
constexpr int32_t GB_GRID_GAP     = 10;
constexpr int32_t GB_GRID_PAD     = 8;

/// Whether a grid tile tap animates to its page. Off by default: an animated
/// scroll from the grid to Music sweeps *through* Watch/Chats/Alerts, i.e.
/// several full-screen redraws for a jump the user already committed to with
/// a tap. See section 7. Swipes stay animated regardless -- that animation is
/// lv_tabview's own, driven by the finger.
constexpr bool GB_GRID_ANIMATE_TAB_CHANGE = false;
```

Also update the file's header comment, which currently says the constants size
"every button and the tab bar".

### 4.2 `gb_ui.cpp` -- new statics

```c
lv_obj_t *s_tabview = nullptr;      ///< needed by the grid tiles and home button
lv_obj_t *s_home_button = nullptr;  ///< status bar; hidden while the grid is showing
```

### 4.3 `gb_ui.cpp` -- the grid page

A new `buildGridTab()` next to the other `build*Tab()` functions, using LVGL's
grid layout so the tiles divide the page evenly at any panel size:

```c
struct GbGridEntry {
    const char *icon;
    const char *name;
    uint32_t    tab;       ///< index in the tabview (1..4)
};

const GbGridEntry GB_GRID_ENTRIES[] = {
    {LV_SYMBOL_HOME,     "Watch",  GB_TAB_WATCH},
    {LV_SYMBOL_ENVELOPE, "Chats",  GB_TAB_CHATS},
    {LV_SYMBOL_BELL,     "Alerts", GB_TAB_ALERTS},
    {LV_SYMBOL_AUDIO,    "Music",  GB_TAB_MUSIC},
};

void gridTileClicked(lv_event_t *event)
{
    uint32_t tab = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    lv_tabview_set_active(s_tabview, tab,
                          GB_GRID_ANIMATE_TAB_CHANGE ? LV_ANIM_ON : LV_ANIM_OFF);
    refreshHomeButton();      // set_active() does NOT send VALUE_CHANGED -- see 5.1
}

void buildGridTab(lv_obj_t *tab)
{
    static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(tab, cols, rows);
    lv_obj_set_style_pad_all(tab, GB_GRID_PAD, 0);
    lv_obj_set_style_pad_row(tab, GB_GRID_GAP, 0);      // LVGL 9.2 has no pad_gap;
    lv_obj_set_style_pad_column(tab, GB_GRID_GAP, 0);   // grid reads row/column

    const uint32_t count = sizeof(GB_GRID_ENTRIES) / sizeof(GB_GRID_ENTRIES[0]);
    for (uint32_t i = 0; i < count; i++) {
        // one lv_button per entry, stretched into its cell, with an
        // icon label (fontHuge) over a name label (fontBody) in a
        // centered flex column -- same makeLabel() helper as everywhere else
        ...
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % GB_GRID_COLS, 1,
                             LV_GRID_ALIGN_STRETCH, i / GB_GRID_COLS, 1);
        lv_obj_add_event_cb(tile, gridTileClicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)GB_GRID_ENTRIES[i].tab);
    }
}
```

An `enum { GB_TAB_GRID = 0, GB_TAB_WATCH, GB_TAB_CHATS, GB_TAB_ALERTS,
GB_TAB_MUSIC }` next to it keeps the indices named rather than magic.

Tile sizing sanity check, worst case (Ultra, `usable_area_rect()` = 338x430,
status bar 40): each tile is about `(338 - 2*8 - 10)/2 = 156` wide and
`(430 - 40 - 2*8 - 10)/2 = 182` tall. On the S3 (240x240, `BEZEL_RADIUS = 0`,
status bar 24): about 108x94. Both comfortably above a finger target.

### 4.4 `gb_ui.cpp` -- status bar home button

`buildStatusBar()` (`gb_ui.cpp:871`) gains a leading button and reads its
height from the new metric instead of the inline `s_small_screen ? 20 : 30`:

```c
s_home_button = makeIconButton(bar, LV_SYMBOL_LIST, homeClicked);   // new small-button helper
lv_obj_set_ext_click_area(s_home_button, GB_STATUS_BUTTON_EXT_CLICK);
```

`LV_SYMBOL_LIST` rather than `LV_SYMBOL_HOME`, since the Watch tile already
uses `LV_SYMBOL_HOME` and the two would read as the same destination.

```c
void homeClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_tabview_set_active(s_tabview, GB_TAB_GRID, LV_ANIM_ON);
    refreshHomeButton();
}

/// The grid is where the button goes, so on the grid it has nothing to do.
void refreshHomeButton()
{
    const bool on_grid = lv_tabview_get_tab_active(s_tabview) == GB_TAB_GRID;
    // LV_OBJ_FLAG_HIDDEN, not delete/create: the flex row keeps its spacing
    // stable so the link label does not jump left and back.
    lv_obj_set_style_opa(s_home_button, on_grid ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    on_grid ? lv_obj_add_state(s_home_button, LV_STATE_DISABLED)
            : lv_obj_remove_state(s_home_button, LV_STATE_DISABLED);
}
```

### 4.5 `gb_ui.cpp` -- `gb_ui_begin()`

```c
 s_tabview = lv_tabview_create(screen);
 lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
 lv_tabview_set_tab_bar_size(s_tabview, 0);
 lv_obj_add_flag(lv_tabview_get_tab_bar(s_tabview), LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_width(s_tabview, LV_PCT(100));
 lv_obj_set_flex_grow(s_tabview, 1);

 buildGridTab(lv_tabview_add_tab(s_tabview, "Home"));     // GB_TAB_GRID
 buildWatchTab(lv_tabview_add_tab(s_tabview, "Watch"));
 buildChatsTab(lv_tabview_add_tab(s_tabview, "Chats"));
 buildAlertsTab(lv_tabview_add_tab(s_tabview, "Alerts"));
 buildMusicTab(lv_tabview_add_tab(s_tabview, "Music"));

 lv_obj_add_event_cb(s_tabview, tabChanged, LV_EVENT_VALUE_CHANGED, NULL);  // swipe path
 lv_tabview_set_active(s_tabview, GB_TAB_GRID, LV_ANIM_OFF);
 refreshHomeButton();
```

Both `lv_tabview_set_tab_bar_size(..., 0)` *and* the `HIDDEN` flag: the size
call zeroes the strip, the flag makes the tabview's flex layout skip it
entirely (`lv_flex.c` skips `LV_OBJ_FLAG_HIDDEN` children) so no theme
padding/border of the bar survives as a sliver.

`build*Tab()` bodies are untouched.

### 4.6 `gb_ui.h` / `app_setup.cpp` (optional but recommended)

Export one function so entering the screen always starts at the launcher
rather than wherever the user left off:

```c
/// Return the Gadgetbridge screen to its launcher grid. Safe before/after
/// gb_ui_begin(); no-op if the grid is already showing.
void gb_ui_show_home(void);
```

Call it from `onHomeGesture()` in `app_setup.cpp:43`, just before
`lv_screen_load_anim(screen_gadgetbridge, ...)`. Without this, swiping down
from the watch face drops the user back into, say, Music.

### 4.7 `app_setup.cpp` -- don't act on gestures that are really scrolls

This is the fix for what section 5.2 used to file as a latent conflict. It is
required by this plan (left/right now means "change tab") and it also closes a
bug that exists in the app today.

**The problem.** `indev_gesture()` (`lv_indev.c:1269`) runs on every
`LV_EVENT_PRESSING`, immediately after `lv_indev_scroll_handler()`, and it does
not care whether a scroll is in progress. So one finger drag produces *both* a
scroll and, once it passes `LV_INDEV_DEF_GESTURE_LIMIT` (50px), an
`LV_EVENT_GESTURE` that bubbles up to the screen -- every object gets
`LV_OBJ_FLAG_GESTURE_BUBBLE` by default, and the climb only stops at the screen
itself, which has no parent to inherit the flag from.

Two consequences, one new and one pre-existing:

- *New:* a left/right swipe over the tabview scrolls to the next tab **and**
  fires a `LV_DIR_LEFT`/`LV_DIR_RIGHT` gesture at `onGadgetbridgeGesture()`.
  Harmless only because that handler ignores those two directions today; it
  makes left/right unusable at screen level for as long as it stays unguarded.
- *Pre-existing:* dragging more than 50px vertically to scroll the Alerts or
  Chats list also fires `LV_DIR_TOP`/`LV_DIR_BOTTOM`, so
  `onGadgetbridgeGesture()` jumps to the watch face or opens the quick-settings
  tray *while the user is scrolling a list*. This follows from the code path
  above rather than from a run on hardware -- confirm it in the emulator with a
  list long enough to scroll (section 8) before treating the fix as a fix.

**The fix.** Ask LVGL whether this touch already belongs to a scroll:

```c
/// True when the drag that produced this gesture is already scrolling
/// something. LVGL claims a scroll at 10px of travel
/// (LV_INDEV_DEF_SCROLL_LIMIT) but only fires LV_EVENT_GESTURE at 50px
/// (LV_INDEV_DEF_GESTURE_LIMIT, both in lv_indev.c), so by the time a gesture
/// arrives, scroll ownership is already settled and this is reliable.
bool gestureOwnedByScroll(lv_indev_t *indev)
{
    return lv_indev_get_scroll_obj(indev) != NULL;
}

void onGadgetbridgeGesture(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || gestureOwnedByScroll(indev)) {
        return;              // scrolling a list, or swiping between tabs
    }
    ...unchanged: LV_DIR_TOP -> watch face, LV_DIR_BOTTOM -> quick settings
}
```

**Why this gives the right behaviour and not just silence.**
`lv_indev_find_scroll_obj()` (`lv_indev_scroll.c:258`) only claims an object
that can *actually* scroll further in the drag's direction -- it checks
`lv_obj_get_scroll_top()`/`_bottom()` and drops directions where the value is
0. So:

| Drag | `scroll_obj` | Result |
|---|---|---|
| Horizontal, anywhere on a tab | tabview content | tab change, no screen gesture |
| Vertical on a list with room left | the list | list scrolls, screen stays put |
| Vertical on a list already at its end | none | screen gesture fires: watch face / tray |
| Vertical on Watch, Music or the grid | none | screen gesture fires, exactly as today |

That is "scroll first, gesture when there is nothing left to scroll", which is
the behaviour a phone gives and the one the existing up/down navigation was
written assuming.

One subtlety worth knowing: the tabview content is claimed for horizontal drags
even at the first and last tab. A snap container with two or more snappable
children short-circuits the overflow test to "assume scrolling"
(`lv_indev_scroll.c`, the `snap_cnt == 2` branches), so there is no edge case
where swiping left on Music leaks a gesture to the screen.

Apply the same guard to `onHomeGesture()` for symmetry. It changes nothing
today -- the watch face has no scrollable widgets -- but it stops the next
scrollable thing added to that screen from re-introducing the same bug.

## 5. Gotchas

### 5.1 `lv_tabview_set_active()` does not send `LV_EVENT_VALUE_CHANGED`

`lv_tabview.c` sends `VALUE_CHANGED` only from `cont_scroll_end_event_cb()` --
the swipe path. A grid tile tap or the home button calls `set_active()`
directly and fires nothing. Hence `refreshHomeButton()` is called explicitly in
both handlers *and* from the `VALUE_CHANGED` callback. Any future
per-tab chrome must do the same; a single `tabChanged()` used by all three
paths keeps that in one place.

### 5.2 Gestures still bubble to the screen handler

Fixed by section 4.7 rather than tolerated -- see there for the mechanism. What
remains true afterwards: a gesture *does* still reach the screen whenever no
scroll claimed the touch, so up/down keep working from every page, and
left/right stay reserved for tab navigation.

### 5.3 Hidden tab bar buttons still exist

`lv_tabview_add_tab()` creates a button in the tab bar for every page and adds
it to the default group if one exists. Hidden and zero-sized, they are
untappable and invisible; they are only worth remembering if keypad/encoder
input is ever added to this app, where they would show up as focusable stops.

### 5.4 Nothing else indexes tabs

`grep` confirms `lv_tabview` appears only in `gb_ui_begin()`, so inserting the
grid at index 0 shifts no other code. The modals, the thread view
(`lv_layer_top()`), and every `refresh*()` are index-agnostic.

### 5.5 Relationship to `plans/full-usable-area-layout.md`

That plan's section 4 is an open question about the tab bar being a strip the
app cannot place itself, stuck in the bezel's bottom curve where it is
narrowest. Removing the tab bar dissolves that question: the Gadgetbridge
screen becomes status bar + content only, which is exactly the two-band shape
that plan wants. The two changes are independent and can land in either order;
if this one lands first, that plan's section 4 and its `GB_TAB_BAR_HEIGHT_*`
references should be struck.

## 6. Step order

1. Metrics: add the new constants, delete `GB_TAB_BAR_HEIGHT_*`, fix the header
   comment.
2. `gb_ui.cpp`: statics, tab-index enum, `makeIconButton()` helper.
3. `buildGridTab()` + `gridTileClicked()`.
4. Status bar: home button, `homeClicked()`, `refreshHomeButton()`, height from
   the metric.
5. `gb_ui_begin()`: hide the bar, register the grid as page 0, wire
   `VALUE_CHANGED`, start on the grid.
6. `app_setup.cpp`: the `gestureOwnedByScroll()` guard in
   `onGadgetbridgeGesture()` (and `onHomeGesture()`). Independent of the rest --
   it can land first, on its own, as a bug fix.
7. `gb_ui_show_home()` + the `app_setup.cpp` call.
8. Update `gb_ui.cpp`'s file header comment ("Four tabs plus modal overlays"
   becomes a launcher grid plus four pages) and strike section 4 of
   `plans/full-usable-area-layout.md`.

## 7. What this costs at runtime

The navigation change touches three things that run on the render path. All
three were checked against LVGL 9.2.2's source rather than assumed.

**A tap on a grid tile: `lv_tabview_set_active()` forces a layout pass.** It
opens with `lv_obj_update_layout(obj)` ("to be sure `lv_obj_get_content_width`
will return valid value"). That is not a tabview-local call:
`lv_obj_update_layout()` resolves the *screen* and, while
`scr->scr_layout_inv` is set, runs `layout_update_core(scr)` over the entire
screen tree -- every object, including all the rows of both lists. Cheap when
the tree is clean (the flag is clear, the loop body never runs, cost is a
parent walk to the screen); a full O(objects) pass with `lv_layout_apply()` on
each dirty container when it is not. In practice a tap arrives with a clean
tree, and when it doesn't, LVGL was going to do that pass in the same frame
anyway -- `set_active()` just pulls it forward. Nothing to design around, but
it is the reason not to call `set_active()` from a periodic refresh.

**The explicit `refreshHomeButton()` calls (section 5.1) are free.** Two style
writes on one 34px button, at most once per tab change, each invalidating only
that button's area. Calling it from all three paths instead of relying on
`LV_EVENT_VALUE_CHANGED` costs nothing measurable; the reason to route them
through one `tabChanged()` is correctness and one place to edit, not speed.

**The animation is the one real cost, and it is why
`GB_GRID_ANIMATE_TAB_CHANGE` defaults off.** `lv_tabview_set_active(..,
LV_ANIM_ON)` animates the content container's `scroll_x` continuously, so a
tap on Music from the grid slides *through* Watch, Chats and Alerts: every
frame of that ~200-300ms is a full-screen redraw with two pages intersecting
the viewport, on a 410x502 panel over QSPI with no 2D acceleration. A swipe
costs the same, but there the user is dragging the pixels themselves and the
motion is the point; for a tap on a tile it is latency in front of a
destination already chosen. `LV_ANIM_OFF` makes a tile tap one redraw.

Two smaller effects, both in the good direction:

- **Object count is roughly a wash.** The grid page adds ~13 objects (4 tiles,
  8 labels, the page); hiding the tab bar retires 8 (4 buttons, 4 labels) from
  the layout and from every redraw of that strip. Call it +5 objects, ~1KB.
- **Per-frame draw work drops slightly** on the pages that matter: the tab bar
  was a strip of four themed buttons redrawn as part of every scroll frame, and
  it is gone.

One thing to *avoid* adding: the default theme gives `lv_button` a shadow, and
four large shadowed tiles redrawn every frame of a swipe is exactly the kind of
blend work that shows up as jank on this SoC. `lv_obj_set_style_shadow_width(
tile, 0, 0)` on the tiles, and lean on background colour for the tile edges.

## 8. Testing

Emulator first -- it exercises the whole UI without hardware:

```bash
pio run -e emulator_watch_ultra -t exec    # 410x502, BEZEL_RADIUS 120
pio run -e emulator_twatchs3   -t exec     # 240x240, BEZEL_RADIUS 0 -- the tight case
```

`gb_link_stdio.cpp` stands in for the phone, so notifications/music/calls can
be driven from the terminal to populate Chats, Alerts and Music before checking
the navigation.

Check, on both emulator sizes:

- grid shows on entry; all four tiles are square-ish, evenly spaced, none
  clipped by the bezel on the Ultra;
- each tile opens its page, animated, and the home button returns from it;
- swipe left/right cycles Grid -> Watch -> Chats -> Alerts -> Music and back,
  including a swipe that *starts on a list row* in Chats/Alerts;
- vertical drag on those lists still scrolls the list, does not change tab,
  **and no longer bounces the user to the watch face / opens the tray**
  mid-scroll (section 4.7 -- needs a list longer than the screen, so send
  enough notifications over the stdio link first);
- with such a list scrolled fully to its end, a further swipe up *does* leave
  for the watch face -- the guard must not swallow the gesture permanently;
- swipe up still returns to the watch face; swipe down still opens the
  quick-settings tray; both work from every page;
- opening a conversation from Chats, and a popup arriving (message, call, find
  device, alarm), still overlay correctly and do not leave the tabview
  mid-scroll;
- the reclaimed 48px really goes to the lists (Alerts shows one more row).

Then on hardware (`pio run -e twatch_ultra -t upload`), re-check the swipe
cases with a real finger -- the emulator's mouse drag is more precise than a
thumb, and the gesture/scroll split (5.2) is the part most likely to feel
different there.

## 9. Deliberately out of scope

- Unread badges on the Chats/Alerts tiles. Cheap later --
  `refreshNotifications()`/`refreshChats()` already compute the counts -- but
  it is a content change, not a navigation one.
- Making the grid configurable/reorderable.
- Tiles for anything outside the four existing pages. Note that
  [`settings-page.md`](settings-page.md) section 5 adds one: Settings opens a
  *screen*, not a tab, so `GbGridEntry` grows an `open()` callback alongside
  `tab`, and the grid becomes 2x3 with the last cell empty. Tile heights drop
  from ~182 to ~118 px on the Ultra and from ~94 to ~60 px on the S3 -- check
  the S3 first. That change belongs to whichever of the two plans lands
  second.
- Any change to the watch-face screen's own navigation.
