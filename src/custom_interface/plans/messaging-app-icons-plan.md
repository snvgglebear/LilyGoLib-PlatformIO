# Plan: per-app icons for Chats/Alerts (SMS, MMS, WhatsApp, Signal, Discord)

## Context

`src/custom_interface/gadgetbridge_ble/` (the app that shipped as `src/gadgetbridge` in older docs — CLAUDE.md's path is stale) shows every phone notification with a single generic icon: the Alerts list always uses `LV_SYMBOL_BELL`, and the Chats list only distinguishes "has a phone number" (`LV_SYMBOL_CALL`) from "doesn't" (`LV_SYMBOL_ENVELOPE`). The phone already tells us which app a message came from (`GbNotification.src` / `GbConversation.app`), and there's already a classifier list of app names (`isMessagingApp()` in `gb_messages.cpp`) that includes `sms`, `mms`, `whatsapp`, `signal`, `discord` among others — but nothing today turns that name into a distinct icon. The goal is recognizable icons for Discord, SMS, MMS, WhatsApp and Signal specifically.

LVGL's built-in symbol font (`LV_SYMBOL_*`) has no brand logos, only generic glyphs (envelope, bell, call, list, ...). Decisions made:
- **SMS/MMS** use generic LVGL symbols (no branding needed).
- **WhatsApp/Signal/Discord** get real bitmap logos, converted to LVGL image C-arrays the same way `src/custom_interface/images/img_*.c` already does it for the launcher icons. The actual logo artwork will be sourced separately (not part of this change) — this plan wires up everything so dropping in three finished `.c` files is the only remaining step.
- Icons appear in **both** the Chats list and the Alerts list.

## Approach

### 1. Central app→icon lookup (new, in `gb_messages.h`/`.cpp`)

Add one function next to the existing `isMessagingApp()` helper (`gb_messages.cpp:38-53`), since that's already the place that knows the list of app name substrings:

```cpp
// gb_messages.h, alongside GbMessageStore
const void *gb_icon_for_app(const std::string &app);
```

Implementation in `gb_messages.cpp`: lowercase the input (reuse whatever lowercasing helper `isMessagingApp` already uses) and match substrings, most-specific first:
- `"whatsapp"` -> `&img_whatsapp`
- `"signal"` -> `&img_signal`
- `"discord"` -> `&img_discord`
- `"sms"` or `"mms"` -> `LV_SYMBOL_ENVELOPE`
- anything else -> `nullptr` (caller keeps its current fallback icon)

Returning `nullptr` for the "no match" case lets both call sites keep their existing fallback behavior (tel-vs-no-tel for chats, bell for alerts) exactly as-is for apps this doesn't recognize, so this is purely additive.

### 2. New image declarations (`src/custom_interface/images/`)

Add to `images.h` under a new `/*Messaging apps ---*/` section:
```cpp
LV_IMAGE_DECLARE(img_whatsapp);
LV_IMAGE_DECLARE(img_signal);
LV_IMAGE_DECLARE(img_discord);
```
Add placeholder `img_whatsapp.c`, `img_signal.c`, `img_discord.c` beside the existing `img_*.c` files. Since real artwork isn't ready yet, generate these as small solid-color placeholder bitmaps (reuse the existing RGB565A8 format so they compile and link now) — swap in real converted logos later without touching any other file.

Note: these are list-row icons, not launcher tiles, so make them a smaller square (e.g. ~24x24 or whatever LVGL renders list-button icons at natively) rather than matching the launcher's 80x80 — check how `lv_list_add_button`'s icon is sized/scaled in this LVGL version before picking dimensions, and size the placeholders (and the eventual real art) to match.

### 3. Wire into the two render sites in `gb_ui.cpp`

Add `#include "gb_messages.h"` and `#include "../images/images.h"` (adjust relative path to match the actual `images/` location) to `gb_ui.cpp`.

**Alerts** (`gb_ui.cpp:877-879`):
```cpp
const void *icon = gb_icon_for_app(notification.src);
lv_obj_t *button = lv_list_add_button(s_notification_list, icon ? icon : LV_SYMBOL_BELL,
                                      summarise(notification).c_str());
```

**Chats** (`gb_ui.cpp:918-921`):
```cpp
const void *icon = gb_icon_for_app(conversation.app);
if (!icon) {
    icon = conversation.tel.empty() ? LV_SYMBOL_ENVELOPE : LV_SYMBOL_CALL;
}
lv_obj_t *button = lv_list_add_button(s_chat_list, icon, text.c_str());
```

This is also what finally makes use of `GbConversation.app` (currently set at `gb_messages.cpp:110` but never read).

### 4. Follow-up (outside this change)

Once real WhatsApp/Signal/Discord logo source images are available, convert them (LVGL's online image converter or `lv_img_conv`, RGB565A8, matching the placeholder dimensions from step 2) and overwrite the three placeholder `.c` files — no other file needs to change.

## Files touched

- `src/custom_interface/gadgetbridge_ble/gb_messages.h` — declare `gb_icon_for_app()`
- `src/custom_interface/gadgetbridge_ble/gb_messages.cpp` — implement it near `isMessagingApp()`
- `src/custom_interface/gadgetbridge_ble/gb_ui.cpp` — use it at the two list-building sites (lines ~878, ~918-921), add includes
- `src/custom_interface/images/images.h` — three new `LV_IMAGE_DECLARE`s
- `src/custom_interface/images/img_whatsapp.c`, `img_signal.c`, `img_discord.c` — new placeholder bitmaps

## Verification

- Build the emulator env (`pio run -e emulator_watch_ultra` or whichever env matches the board in use) to confirm it compiles and links with the new image files.
- Run the emulator (`pio run -e emulator_watch_ultra -t exec`), drive a few `notify` messages through the stdio link (see `src/custom_interface/gadgetbridge_ble` / README for the emulator test harness) with `src` set to `"WhatsApp"`, `"Signal"`, `"Discord"`, `"SMS"`, and something unrecognized, and confirm each shows the right icon in both the Chats and Alerts screens, with unrecognized apps still falling back to the old envelope/call/bell icons.
