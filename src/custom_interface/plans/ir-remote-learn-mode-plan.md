# Plan: IR remote — multi-code list + learn/receive mode

Companion to `src/new_interface/ui_ir_remote.cpp`, which today is a single-code,
send-only screen copied unchanged from `src/factory/ui_ir_remote.cpp`. This
plan covers turning it into a persisted list of named codes with a capture
("learn") flow, per `src/custom_interface/plan.md`'s "ir remote functionality
to control other devices." **Plan only — no code changes in this pass.**

---

## 1. What already works (verified against this repo, 2026-08-15)

### 1.1 Sending a single hardcoded NEC code works today, on one board only

`ui_ir_remote_enter()` (`src/new_interface/ui_ir_remote.cpp:79-114`) builds a
hex textarea + Send/Back button pair. The code is held in one file-scope
variable, not persisted:

```cpp
static uint32_t nec_code = 0x12345678;  // LilyGo Factory ir remote test nec code
...
static void send_event_handler(lv_event_t *e)
{
    hw_feedback();
    hw_set_remote_code(nec_code);
}
```
(`src/new_interface/ui_ir_remote.cpp:27,42-46`)

`hw_set_remote_code()` really does transmit, on real hardware:

```cpp
void hw_set_remote_code(uint32_t nec_code)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
    static bool isBegin = false;
    if (!isBegin) { isBegin = true; irsend.begin(); }
    irsend.sendNEC(nec_code);
#endif
}
```
(`src/factory/hal_interface.cpp:2284-2294`; `src/new_interface/hal_interface.cpp`
is byte-identical here — confirmed with `diff`)

This is real, working NEC transmit via the `IRsend` class from
`tonhuisman/IRremoteESP8266` (a C++20-compatibility fork of the
`crankyoldgit/IRremoteESP8266` library), on `IR_SEND` = GPIO2
(`variants/lilygo_twatch_s3/pins_arduino.h:41`). **Only NEC** is ever sent —
`sendNEC()` is called directly, never the library's generic multi-protocol
`send()`.

### 1.2 The receive-side HAL entry points exist but are dead code everywhere

`hal_interface.h` declares two more IR functions beyond send, with doc
comments that read as if receive is implemented:

```cpp
/// True to enable sending, false to enable receiving.
void hw_ir_function_select(bool enableSend);
/// Get the remote control code... received by the IR receiver.
void hw_get_remote_code(uint64_t &result);
```
(`src/new_interface/hal_interface.h:1315-1331`)

Their bodies say otherwise:

```cpp
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
#include <IRremoteESP8266.h>
#include <IRrecv.h>
IRrecv irrecv(IR_SEND); // T-Watch S3 GPIO15 pin to use.
#endif

void hw_get_remote_code(uint64_t &result)
{
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
    decode_results results;
    if (irrecv.decode(&results)) {
        result = results.value;
        irrecv.resume();
    }
#else
    result = random(0, INT_MAX);
#endif
}

void hw_ir_function_select(bool enableSend)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
    if (enableSend) { instance.IRFunctionSelect(IR_FUNC_SENDER); irrecv.disableIRIn(); }
    else            { instance.IRFunctionSelect(IR_FUNC_RECEIVER); irrecv.enableIRIn(); }
#endif
}
```
(`src/factory/hal_interface.cpp:2278-2322`, identical in `src/new_interface`)

**`USING_IR_RECEIVER` is never defined anywhere in this repo** — not in
`platformio.ini`, not in any `variants/*/pins_arduino.h`, not in `boards/*.json`
(checked all three; grep for the macro across the whole tree returns nothing).
Consequences, true on **every** currently buildable configuration, including
real T-Watch-S3 hardware:

- `hw_get_remote_code()` always takes the `#else` branch: it returns
  `random(0, INT_MAX)`, a placeholder, never a decoded IR signal.
- `hw_ir_function_select()`'s body is compiled out entirely (its guard needs
  `USING_IR_RECEIVER` too) — calling it today does nothing, on hardware or in
  the emulator.
- `<IRrecv.h>`/`<IRremoteESP8266.h>` and the `irrecv` object are never even
  compiled in, since they're gated by the same undefined macro.
- The `IRrecv irrecv(IR_SEND)` line's own comment claims **"T-Watch S3 GPIO15
  pin to use"**, but the macro actually passed is `IR_SEND`, which
  `pins_arduino.h:41` defines as GPIO2 — the same pin used for transmit. There
  is no `IR_RECV`/GPIO15 macro defined anywhere. This is either a stale
  comment or a sign the receive diode isn't wired the way the comment
  describes — **needs verification against a physical board before any real
  receive wiring is attempted.**

**Bottom line for scoping learn mode:** the HAL *shape* for receive exists
(`hw_get_remote_code`, `hw_ir_function_select`), but there is no working
receive/decode path in this codebase today. Learn mode's UI/data-model side
can be built and fully exercised now (against the existing random()
placeholder, which is actually a convenient stand-in for "a code arrived" in
the emulator); making it capture *real* signals is separate firmware work,
gated on resolving §1.2's open pin question and turning on
`USING_IR_RECEIVER`. See §5 and the order-of-work list in §7.

### 1.3 The library supports far more than what's wired up

`IRremoteESP8266`'s `decode_results` (per the upstream library's documented
API — the fork isn't vendored/fetched in this workspace, so re-verify field
names against it before implementing) exposes `decode_type` (a protocol enum
covering dozens of formats: NEC, SONY, RC5, SAMSUNG, LG, Panasonic, ...),
`address`, `command`, `value` (up to 64 bits — note `hw_get_remote_code`
already takes a `uint64_t&`, wider than the `uint32_t` `hw_set_remote_code`
takes), `bits` (decoded bit length), and a `repeat` flag. `IRsend` likewise
has a generic `send(decode_type_t type, uint64_t data, uint16_t nbits)` in
addition to protocol-specific helpers like `sendNEC()`. None of that is used
today — send is hardcoded to NEC, and even the dead receive path only ever
captured `results.value`, discarding protocol and bit-length. A real learn
mode that faithfully replays *whatever* remote it captured needs the data
model and the HAL to carry protocol + bit-length, not just a raw integer
(§3.1, §5.3).

### 1.4 Board/env scope: this app only exists on one watch

`USING_IR_REMOTE` gates the whole app, both the launcher tile and the
`ui_ir_remote.cpp` translation unit (`#if defined(USING_IR_REMOTE)` at
`src/new_interface/ui_ir_remote.cpp:23`). It is defined in exactly two
places:

- `variants/lilygo_twatch_s3/pins_arduino.h:74` — real T-Watch-S3(-Plus)
  hardware.
- `platformio.ini:324`, the `[env:emulator_twatchs3]` build flags.

It is **not** defined for `twatch_ultra`, `tlora_pager`, `emulator_watch_ultra`,
or `emulator_lora_pager` (`platformio.ini:227-252, 306-336`) — those boards
have no IR emitter and never build or show this app
(`src/new_interface/ui_main.cpp:773-776`, `create_app(panel, "IR Remote", ...)`
inside the same `#if defined(USING_IR_REMOTE)`). The `IRremoteESP8266` fork
library is likewise only added to `[env:twatchs3]`'s `lib_deps`
(`platformio.ini:223-225`) — not to `emulator_twatchs3`'s, which is fine
because the library-using code in `hal_interface.cpp` is additionally gated
on `ARDUINO`, so the emulator never needs to link it.

**Everything in this plan is scoped to T-Watch-S3 hardware and
`emulator_twatchs3`.** T-Watch-Ultra and T-LoRa-Pager users never see this
app under the current build-flag wiring — worth confirming that's acceptable
given `custom_interface/plan.md` doesn't call this out as S3-specific (see
§6 risks).

### 1.5 Persistence: NVS already exists in this codebase — this is not a new addition

The task brief's premise was that this app only has the `RTC_DATA_ATTR`
(deep-sleep-only) convention to draw on. That's not quite right: a real
ESP32-NVS-backed `Preferences` pattern **already exists** in
`hal_interface.cpp`, for a different feature:

```cpp
#define NVS_NAME    "pager"
...
static Preferences prefs;   // NVS handle for the NVS_NAME namespace
static user_setting_params_t user_setting;
...
prefs.begin(NVS_NAME);
if (prefs.getBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t))
        != sizeof(user_setting_params_t)) {
    log_e("Data is not correct size!,set default setting");
    user_setting.brightness_level = 50;
    ...
    prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
}
```
(`src/new_interface/hal_interface.cpp:42-48, 81, 921-934`)

and the setter:

```cpp
void hw_set_user_setting(user_setting_params_t &param)
{
    user_setting = param;
#ifdef ARDUINO
    prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
#endif
}
```
(`src/new_interface/hal_interface.cpp:978-983`)

This is a **single fixed-size blob** written under one NVS key
(`prefs.putBytes(NVS_NAME, ...)` — the key and the namespace are literally
the same string here), holding one `user_setting_params_t` struct
(`src/new_interface/hal_interface.h:314-329`: brightness, keyboard backlight,
display timeout, charger current/enable). On the emulator (`#else` branch,
`hal_interface.cpp:953-960`) there is no `Preferences` at all — the struct is
just reset to fixed defaults every run, since desktop `Preferences` isn't
available under the native/SDL2 build.

There is also a real `nvs` partition already provisioned:
`nvs, data, nvs, 0x9000, 0x5000,` — 20 KB (`src/factory/partitions.csv`,
shared by all three hardware envs via `board_build.partitions` in
`platformio.ini`).

**Implication for this feature:** IR codes should reuse this exact
`Preferences`/NVS pattern (it's the established convention, not a scope
addition), but **not** the same blob or namespace — see §3.2 for why, and for
why a *list* doesn't fit the existing single-key single-struct shape without
some adaptation.

`RTC_DATA_ATTR` (`src/factory/ui_main.cpp:121-126`, used for
`brightness_level`/`keyboard_level`) is the *other* persistence mechanism in
this codebase, but it only survives deep sleep, not a full power cycle — it
is the wrong tool for "codes the user captured from a physical remote and
expects to still be there after the watch's battery dies," and is not
recommended here.

---

## 2. UI redesign

### 2.1 Reuse the existing list pattern from the Music app, not a bespoke widget

This codebase already has a working "scrollable list of named items, tap to
act, per-row data" screen — the file browser in `ui_audio.cpp`:

```cpp
lv_obj_t *list1 = lv_list_create(main_page);
...
lv_obj_t *obj, *label;
int index = 0;
for (auto file_info : music_list) {
    string file_name = ...;
    obj = lv_list_add_button(list1, LV_SYMBOL_AUDIO, file_name.c_str());
    lv_obj_set_user_data(obj, &(music_list[index]));
    index++;
    label = lv_label_create(obj);
    lv_label_set_text(label, LV_SYMBOL_PLAY);
    lv_obj_add_event_cb(obj, audio_play_event, LV_EVENT_CLICKED, label);
}
```
(`src/new_interface/ui_audio.cpp:181-204`)

This is the template for the new `ui_ir_remote_enter()`: one `lv_list_create()`
inside the existing `create_menu()` container, one `lv_list_add_button(list,
icon, entry.label)` per saved code, `lv_obj_set_user_data()` binding the row
to its backing entry, and a `LV_EVENT_CLICKED` handler that calls
`hw_feedback()` + the send path (generalized past NEC-only, see §5.3) using
that entry's stored protocol/value/bits.

Per-row delete: append a second small `LV_SYMBOL_TRASH` label/button to each
row (same technique as the trailing `LV_SYMBOL_PLAY` label above), routed to
its own `LV_EVENT_CLICKED` handler that opens a confirm dialog via
`create_msgbox()` (`src/new_interface/ui_tools.cpp:199-230`,
already used this way for confirmations elsewhere,
e.g. `src/new_interface/ui_msg.cpp:45-59`'s `ui_msg_pop_up()`) before removing
the entry and rewriting the persisted list.

Empty state (first boot, or after deleting every code): show a centered
`create_text(cont, LV_SYMBOL_WARNING, "No codes yet — tap + to learn one",
LV_MENU_ITEM_BUILDER_VARIANT_1)` row instead of an empty list; the add/learn
button stays reachable via the header/floating button either way.

### 2.2 Keep manual hex entry as a secondary path, not the primary flow

Retain today's textarea + keyboard flow
(`src/new_interface/ui_ir_remote.cpp:89-99`) verbatim as an
"Add code manually" action reachable from the list screen (e.g. a second
option alongside "Learn New Code"), because not every code a user wants is
capturable live — some come from a spec sheet or an online database as a raw
hex value. The difference from today: instead of immediately overwriting the
one global `nec_code` and requiring a separate Send tap, submitting the
textarea should feed the same "prompt for a label, append to the list, save"
path that learn mode uses (§2.3), so both entry methods produce identically
structured list entries.

### 2.3 Save flow (shared by manual entry and learn mode)

1. A candidate code is available (typed hex, or captured — §5.2).
2. Prompt for a label. Reuse the existing textarea+keyboard widgets
   (`ui_ir_remote.cpp:89-102`) inside a small dialog, or `create_msgbox()`
   with an embedded textarea; default suggestion e.g. `"Code N"` (N =
   current count + 1), or the protocol name if learn mode determined one
   (§5.3).
3. On confirm: append to the in-memory list, call the new HAL persistence
   function (§3), pop back to the list screen, which re-renders with the new
   `lv_list_add_button()` row.
4. On cancel: discard the candidate, return to the list unchanged.

---

## 3. Data model + persistence

### 3.1 Struct shape

```c
// app_config.h — single source of truth per custom_interface/plan.md
#define IR_CODE_LABEL_MAX_LEN   24
#define IR_CODE_MAX_ENTRIES     32   // sized against the 20KB nvs partition, see §3.2

typedef struct {
    char     label[IR_CODE_LABEL_MAX_LEN];  // user-entered name, NUL-terminated
    char     device[IR_CODE_LABEL_MAX_LEN]; // optional grouping tag, "" = ungrouped (§4)
    uint8_t  protocol;                      // decode_type_t from IRremoteESP8266, or a
                                             // small local enum if we don't want the HAL
                                             // header pulling in the IR library's types
    uint8_t  bits;                          // decoded bit length; 32 for legacy/manual NEC entries
    uint64_t value;                         // matches hw_get_remote_code()'s existing uint64_t width
} ir_code_entry_t;

typedef struct {
    uint8_t version;    // format version — see §3.3
    uint8_t count;
    ir_code_entry_t entries[IR_CODE_MAX_ENTRIES];
} ir_code_list_t;
```

`value` is `uint64_t` to match what `hw_get_remote_code()` already returns
(§1.3) rather than the `uint32_t` `hw_set_remote_code()` currently takes —
sending needs to grow to accept the wider type once non-NEC protocols are
supported (§5.3).

### 3.2 New NVS namespace, not the existing "pager" blob

Recommend a **separate** `Preferences` namespace (e.g. `"ir_codes"`), storing
`ir_code_list_t` as one blob via `putBytes`/`getBytes`, following the exact
mechanics already in `hal_interface.cpp:921-934` — same library, same
begin/getBytes/putBytes shape, new namespace string and new key.

Reasoning against just adding fields to the existing `"pager"`/
`user_setting_params_t` blob:
- That struct's own comment already flags version-skew risk ("a
  `user_setting_params_t` whose layout changed since it was written" —
  `hal_interface.cpp:922-923`) for a *fixed* 5-field settings struct. Folding
  a variable-length list into it multiplies that risk and couples two
  unrelated features' persistence lifetimes.
- A dedicated namespace can be wiped/reset independently (useful for a
  future "clear all IR codes" action) without touching brightness/charger
  settings.
- `sizeof(ir_code_list_t)` at the sizes above is roughly `2 + 32*(24+24+1+1+8)`
  ≈ 1.9 KB — comfortably inside the 20 KB `nvs` partition
  (`src/factory/partitions.csv`) alongside the existing `"pager"` blob, with
  headroom for ESP-IDF NVS's own per-entry overhead.

Rejected alternative: per-entry NVS keys (`"code0"`, `"code1"`, ... +
`"count"`) via `Preferences::putBytes`/`getBytes` per key. This would allow
growth without a hardcoded cap, but adds key-management complexity (rename,
compaction after delete-from-middle) for no real benefit at the scale a
wrist-worn remote needs — a few dozen codes, not thousands. The fixed-array
blob is simpler, mirrors the existing convention exactly, and a single
rewrite-the-whole-blob on every add/delete is cheap at this size.

### 3.3 Version byte

Add a `version` field to `ir_code_list_t` from day one (see §3.1) and check
it the same way the existing code checks blob size
(`hal_interface.cpp:926`): a mismatched version (or wrong `getBytes` return
size) means "first boot or format changed," and the loader resets to an
empty list and rewrites it — same pattern as the existing fallback at
`hal_interface.cpp:927-933`, just generalized to also catch format drift
within the *same* size (e.g. reinterpreting `protocol` values).

### 3.4 Emulator behavior

`Preferences` is `ARDUINO`-only (`hal_interface.cpp:75`). Mirror the existing
`#else` fallback pattern (`hal_interface.cpp:953-960`, which resets
`user_setting` to fixed defaults every run on the emulator) for the new list:
start from a small in-RAM seeded list (2-3 plausible demo entries) each
emulator run, so the list/send/delete UI is fully exercisable on desktop per
`custom_interface/plan.md`'s "make as much as possible runnable/testable in
the simulator" directive, without needing real NVS. Learn mode's "capture" in
the emulator can keep using the existing `random(0, INT_MAX)` placeholder
(§1.2) as the simulated incoming code — but see §5.2 for a small
tweak to make repeated presses distinguishable for testing duplicate
handling, rather than a fresh random value every time.

### 3.5 New/changed HAL surface

```c
// hal_interface.h additions
void hw_ir_codes_load(ir_code_list_t &list);              // populate from NVS (or emulator seed list)
bool hw_ir_codes_save(const ir_code_list_t &list);         // persist whole list; false if it didn't fit
```

`hw_get_remote_code()`/`hw_set_remote_code()`/`hw_ir_function_select()`
signatures are addressed separately in §5.3 (they need to grow to carry
protocol/bit-length) — that part depends on the real-receive-hardware work in
§5.1, not on the data-model/persistence work above, which can land first and
independently.

---

## 4. Multi-device organization

`custom_interface/plan.md` says "control other devices" (plural). Recommend:
add the optional `device` field already shown in §3.1 rather than building a
separate two-level device→codes hierarchy. At render time, group the flat
list by `device` and render section dividers with LVGL's list
section-header helper (`lv_list_add_text()` — not currently used anywhere in
this codebase, so verify its exact name/signature against the LVGL version
actually resolved, `lvgl/lvgl @ ^9.4.0` for hardware envs vs `lvgl@9.2.2` for
the emulator per `platformio.ini`, before relying on it); entries with
`device == ""` render ungrouped, e.g. under no header or a generic "Other".

Reasoning: a flat array of entries each carrying an optional tag is trivial
to persist (§3) and edit incrementally — add/delete is still "modify one
entry in one array." A real nested device→codes structure would need its own
list-of-lists persistence and referential-integrity handling (what happens
to codes when a device is renamed or deleted) for no real gain at this scale.
The `device` field is optional on save (§2.3's label prompt can offer it as a
second, skippable field), so simple one-off codes need no extra step, while
users who do want organization get it by typing a device name once per
group.

---

## 5. Learn/receive mode

### 5.1 What has to exist at the firmware level first (real capture)

Per §1.2, none of this is wired up today. Before learn mode can capture a
*real* signal rather than the emulator's placeholder:

1. Resolve the GPIO15-vs-`IR_SEND`(GPIO2) discrepancy in the `irrecv`
   constructor comment (`hal_interface.cpp:2281`) against actual T-Watch-S3
   hardware — confirm which pin the IR receive diode is actually wired to,
   and whether one exists on the shipped board revision at all.
2. Define `USING_IR_RECEIVER` (in `variants/lilygo_twatch_s3/pins_arduino.h`,
   alongside the existing `USING_IR_REMOTE` at line 74) once the pin is
   confirmed, so the `irrecv` object, `<IRrecv.h>` include, and the real body
   of `hw_get_remote_code()`/`hw_ir_function_select()` actually compile in.
3. Confirm `instance.IRFunctionSelect(IR_FUNC_SENDER/RECEIVER)`
   (`hal_interface.cpp:2314-2319`, from LilyGoLib) does what its name implies
   — e.g. whether send and receive genuinely share one pin/mode that must be
   switched, which would explain why the codebase models this as an
   exclusive "select" rather than both being simultaneously active.

This is real hardware-verification work, not something this plan can settle
from source alone — flagged as the top risk in §6, and split out as its own
order-of-work step (§7) so the UI/data-model work isn't blocked on it.

### 5.2 UX flow

1. From the list screen (§2.1), "Learn New Code" (header button or a second
   floating action alongside "Add code manually", §2.2).
2. Full-screen capture prompt: "Point a remote at the watch and press a
   button," with a spinner or `ui_create_process_bar()`
   (`src/new_interface/ui_define.h:146`) styled as an indeterminate wait, and
   a Cancel button. Before waiting, call `hw_ir_function_select(false)` to
   arm receive mode (today a no-op per §1.2 — see §5.1 — but the call site
   belongs here regardless, so the UI is already correct once §5.1 lands).
3. Poll `hw_get_remote_code()` (or its widened replacement, §5.3) on a
   timer. On the real path once §5.1 lands, a nonzero/valid decode ends the
   wait. On the current placeholder path, treat any returned value as "a
   code arrived" — this exercises the full UI flow today.
4. Timeout (e.g. 10-15 s) with nothing captured: show "No signal detected"
   via `ui_msg_pop_up()` (`src/new_interface/ui_msg.cpp:45-59`) and return to
   the list rather than waiting indefinitely.
5. On capture: call `hw_ir_function_select(true)` to restore send mode, then
   feed the captured value into the shared save flow (§2.3).
6. Duplicate handling: if the captured `value` (+`protocol`) already matches
   an existing entry, don't silently reject it — warn ("This code matches
   '<existing label>' — save anyway?") but allow saving, since the same raw
   code legitimately applies to different labeled buttons across universal
   remotes. (This is a judgment call — flagged as a decision point, not
   a certainty, since it depends on how often collisions actually occur in
   practice once real capture exists.)

For emulator testing of this flow before §5.1 lands: make the emulator's
placeholder in `hw_get_remote_code()` cycle through a small fixed set of fake
values (e.g. 3-4 canned NEC-shaped codes) rather than a fresh `random()` every
call, so repeated "learn" presses in the emulator can deterministically
exercise both the "new code" and "duplicate code" paths from §5.2 step 6.

### 5.3 Scope of what learn mode can promise, honestly

Because the receive path realistically starts from `results.value`,
`decode_type`, and `bits` (§1.3) but this codebase's send path is hardcoded
to `irsend.sendNEC()` (§1.1), a captured *non-NEC* code cannot be faithfully
replayed until `hw_set_remote_code()` is also generalized — e.g. a new
`hw_send_ir_code(uint8_t protocol, uint64_t value, uint8_t bits)` that calls
the library's generic `IRsend::send(decode_type_t, uint64_t, uint16_t)`,
falling back to `sendNEC()` when `protocol == NEC` for compatibility with
existing saved/manual entries. Don't promise "any remote, any protocol" in
the UI copy until this generalized send path exists — scope learn mode's
first iteration around whatever protocols the actual library build supports
decoding *and* sending, and verify that list against the vendored fork
(not available in this workspace) before finalizing UI copy that names
specific brands/protocols.

---

## 6. Risks

- **No real receive path exists today** (§1.2) — the single biggest
  open item. Everything about learn mode's *UI and data model* can be built
  and tested now; whether it ever captures a real signal depends on §5.1,
  which needs a physical board and cannot be fully resolved from source
  alone.
- **Stale/contradictory pin comment** (`GPIO15` vs. the `IR_SEND`/GPIO2
  macro actually used, `hal_interface.cpp:2281`) — could mean the comment is
  simply wrong, or that this board revision's receive diode isn't wired the
  way it implies. Resolve before any receive wiring work.
- **Board scope**: this app, and therefore this whole feature, only exists
  on T-Watch-S3 hardware and `emulator_twatchs3` (§1.4). T-Watch-Ultra and
  T-LoRa-Pager never get it under current build flags — confirm that's
  intended, since `custom_interface/plan.md` doesn't call out IR as
  S3-specific.
- **NVS partition capacity**: 20 KB total (`src/factory/partitions.csv`),
  shared with the existing `"pager"` blob and any future NVS consumers.
  `IR_CODE_MAX_ENTRIES` (§3.1) should stay conservative; revisit if it turns
  out too small in practice.
- **Format versioning**: same class of risk the existing
  `user_setting_params_t` comment already flags for a much simpler struct
  (`hal_interface.cpp:922-923`). The `version` byte in §3.3 is cheap
  insurance, added from day one rather than retrofitted after the first
  format change breaks existing saved codes.
- **Library capability vs. what's verified**: §1.3/§5.3's description of
  `decode_results`/generic `IRsend::send()` is based on the upstream
  IRremoteESP8266 API; the actual `tonhuisman` fork isn't vendored in this
  workspace, so re-verify exact field/method names against it before
  implementing.

---

## 7. Order of work

1. **Data model + NVS layer** (§3): new struct(s) in `app_config.h`, new
   `"ir_codes"` namespace, `hw_ir_codes_load`/`hw_ir_codes_save` in
   `hal_interface.cpp`/`.h`, emulator in-RAM seed-list fallback. No UI change
   yet — verifiable by logging load/save round-trips.
2. **UI redesign** (§2.1-2.2): replace the textarea screen with the
   `lv_list`-based list, wire taps to send using existing entries, keep
   manual hex entry reachable as a secondary path feeding the same save flow
   (§2.3). This is the step that must not regress "send a code" — see the
   test matrix (§8).
3. **Per-entry delete** with confirm dialog (§2.1).
4. **Learn-mode UI flow** (§5.2): prompt → wait screen → label/device entry
   → save. Ships fully testable in the emulator against today's placeholder
   receive path (§1.2, §5.2's cycling-fake-value tweak), independent of real
   hardware.
5. **Real receive hardware/firmware work** (§5.1, §5.3): resolve the pin
   question, define `USING_IR_RECEIVER`, widen the send/receive HAL to carry
   protocol + bit-length, verify against a physical board and a second
   reference remote. Cannot be fully validated in the emulator.
6. **Multi-device grouping** (§4): `device` field + section-header rendering
   — additive, can land any time after step 2.

Steps 1-4 are independently shippable as a complete "multi-code list with a
learn-mode UI that works against the current placeholder capture" feature.
Step 5 is what turns "learn mode" from a UI mockup into something that
captures real remotes, and is gated on hardware access this plan cannot
provide.

---

## 8. Test matrix

| Case | Expected |
| --- | --- |
| Send an existing/legacy-style code (regression) | Transmits identically to today's single-code behavior; `hw_set_remote_code`/its generalized replacement still reaches `irsend.sendNEC()` for NEC entries |
| Manual hex entry | Appears in the list with the entered value; sends correctly; feeds the same save flow as learn mode |
| Learn mode captures a known remote's code (real hardware, post-§5.1) | Decoded protocol/bits/value match a reference decode of that remote's button; saved entry replays it correctly |
| Learn mode in the emulator (pre-§5.1) | Placeholder capture completes the UI flow end-to-end (prompt → wait → label → saved list entry) without real hardware |
| Duplicate code learned twice | User is warned but can still save both (§5.2 step 6) — not silently blocked |
| List persistence survives deep sleep | Trivially true — NVS survives deep sleep same as RTC memory |
| List persistence survives full power-off / battery-dead reboot | The real bar for this feature (unlike `RTC_DATA_ATTR`) — verify with an actual unplug/reboot, not just a sleep/wake cycle |
| Multiple codes coexist | N distinct entries each individually sendable and deletable without corrupting the others' stored values |
| Delete removes only the intended entry | List re-renders with N-1 entries; NVS blob rewritten correctly; remaining entries' values unchanged |
| List at 0 / 1 / `IR_CODE_MAX_ENTRIES` entries | Empty state shown at 0; add blocked or handled gracefully at the cap |
| Device grouping (§4) | Entries with the same `device` render under one section header; `device == ""` entries render ungrouped |
| App absence on `twatch_ultra`/`tlora_pager` builds | Build succeeds, no IR Remote tile appears, no crash (unchanged from today — §1.4) |
| Full flow in `emulator_twatchs3` | List, add-manual, learn (simulated), send, delete all exercisable on desktop with no physical board, per `custom_interface/plan.md`'s simulator-testability goal |
