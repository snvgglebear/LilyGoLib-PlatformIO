# `src/gadgetbridge` — Gadgetbridge companion firmware

The watch side of [`.claude/twatch-ultra-ble-protocol.md`](../../.claude/twatch-ultra-ble-protocol.md):
a T-Watch that pairs with [Gadgetbridge](https://codeberg.org/snvgglebear/Gadgetbridge)'s
`TWATCH_ULTRA` device support and shows notifications, calls, music, weather and
alarms — no changes needed on the Android side.

## Build

```bash
# point src_dir at src/gadgetbridge in platformio.ini, then:
pio run -e twatch_ultra -t upload
pio run -e twatch_ultra -t monitor
```

Builds for `twatchs3` and `tlora_pager` too. Note that Gadgetbridge matches on
the *advertised name*, so every board advertises as a T-Watch Ultra regardless
of what it actually is (see `GB_ADVERTISED_NAME_PREFIX` below).

## Try it without hardware

The native/SDL2 emulator build swaps the BLE link for stdin/stdout
(`gb_link_stdio.cpp`), so the whole protocol and UI can be exercised on the
desktop — lines you type go through the same framing and decoding a real write
to the RX characteristic would:

```bash
pio run -e emulator_watch_ultra -t exec
```

```json
{"t":"time","ts":1786060800,"o":-300}
{"t":"notify","id":8231,"src":"Signal","title":"Ada Lovelace","body":"On my way"}
{"t":"call","cmd":"incoming","name":"Grace Hopper","number":"+15551234567"}
{"t":"musicinfo","artist":"Boards of Canada","track":"Dawn Chorus","dur":257}
{"t":"weather","temp":291,"txt":"broken clouds","loc":"Bristol"}
{"t":"alarm","d":[{"h":7,"m":30,"rep":31}]}
{"t":"find","n":true}
```

The watch's own messages are printed back on stdout, so a pipe is a usable
protocol test:

```bash
printf '{"t":"ver"}\n' | pio run -e emulator_watch_ultra -t exec
```

## Pairing

1. Flash, and watch the serial monitor for `[gb] advertising as "T-Watch Ultra XXXX"`.
2. In Gadgetbridge, scan for devices and pick that name. Bonding is off by
   default (§4), so answer "no" if it offers to pair.
3. Gadgetbridge sends `{"t":"ver"}` and `{"t":"time",…}` as soon as it subscribes;
   the clock on the Watch tab jumping to the right time is the sign it worked.

Android caches device names — if you change `GB_ADVERTISED_NAME_PREFIX`, forget
the device in Android's Bluetooth settings and toggle Bluetooth before deciding
it didn't take.

## What the screens do

| Tab | Contents |
| --- | --- |
| Watch | clock (synced by `time`), weather, next alarm, "Ring my phone" (`findPhone`) |
| Chats | SMS and chat messages, threaded per contact — tap for the conversation, reply from it |
| Alerts | everything else; tap one for body text and dismiss / open / mute / canned reply |
| Music | track metadata and the media keys (`music`) |

Modal overlays cover incoming calls (answer/reject, `call`), the phone's "find
device" request, and alarms firing. All three buzz until stood down. A new
message raises its own popup with Open / Reply / Dismiss.

### Messages

The protocol has no message type — an SMS and a Signal message both arrive as
`notify` (§5.3). The watch tells them apart by the fields the phone filled in:
`tel` set means SMS/MMS, `sender` set means somebody said something, and failing
both, a known messaging app name in `src`. Anything message-like is threaded per
correspondent in **Chats** and never also appears in **Alerts**.

Within a thread, received and sent messages read as a conversation. Replies go
out as `notify n:"reply"` carrying the thread's `tel`, which is what makes
Gadgetbridge send a real SMS rather than firing the notification's reply action —
see §6.6 and `android-sms-notifications-plan.md` for the Android side, including
the `type` field that would replace the app-name guess.

## Files

| File | Role |
| --- | --- |
| `gb_protocol.*` | framing (§2) and the JSON codec for §5/§6. No board or LVGL dependency |
| `gb_link.h` | the interface between app and transport |
| `gb_ble.cpp` | NimBLE GATT server: NUS, Device Information, Battery Service (Arduino only) |
| `gb_link_stdio.cpp` | stdin/stdout stand-in for the phone (emulator only) |
| `gb_app.*` | watch-side state and behaviour: clock, notifications, calls, alarms, battery reporting |
| `gb_messages.*` | SMS/chat classification and conversation threading |
| `gb_ui.*` | the LVGL screens |
| `gb_platform.*` | clock, battery and haptics, per board and per build |
| `gadgetbridge.ino` / `main.cpp` | Arduino and native entry points |

## Configuration

Override with `-D` in `platformio.ini`:

| Macro | Default | Effect |
| --- | --- | --- |
| `GB_FW_VERSION` | `"0.1.0"` | reported in `ver` and in Device Information |
| `GB_ADVERTISED_NAME_PREFIX` | `"T-Watch Ultra"` | advertised name, before the MAC suffix. Must match `^T[-_ ]?Watch[-_ ]?(S3[-_ ]?)?Ultra.*$` or Gadgetbridge will not recognise the device |
| `GB_ENABLE_BONDING` | `0` | set to 1 to require pairing (§4) |
| `GB_MAX_NOTIFICATIONS` | `16` | notifications kept on the watch |
| `GB_MAX_CONVERSATIONS` | `8` | message threads kept on the watch |
| `GB_MAX_MESSAGES_PER_CONVERSATION` | `12` | messages kept per thread |

## Conformance (§9)

Everything on the checklist, plus everything in the "worth adding next" list:
`status` **and** Battery Service, `ver`, `notify-`, `call`, `find`, music and
weather. Alarms are stored and actually ring.

Not implemented, and why:

- **Free-text replies.** `notify n:"reply"` is wired up for both notifications
  and conversations, but the watch offers three canned replies rather than an
  on-screen keyboard. Pushing the user's own canned replies from the phone is
  §2.2/§3.3 of `android-sms-notifications-plan.md`.
- **Activity data** (steps, heart rate, sleep). §10 is explicit that this needs
  a schema entity and sample provider on the Android side, not just a message
  type, so it wants designing on both sides at once.
- **`vibrate` intensity.** The DRV2605 plays fixed waveforms rather than taking
  a level, so `n` only picks between a tap and a buzz.

## Android-side work

`android-sms-notifications-plan.md` covers what the Gadgetbridge fork needs for
messaging: confirming the SEND_SMS grant that silent reply failures hinge on,
canned replies, and three additive `notify` fields (`type`, `reply`, `key`) that
would replace the watch's classification heuristics. Written against the
`twatch_ultra` branch of the fork.

## Notes

- Only one Bluetooth host stack can run on an ESP32-S3. This app uses NimBLE and
  nothing else — do not pull the Bluedroid-based BLE Keyboard/Mouse libraries
  into the same image.
- NimBLE callbacks run on the host task. Writes are reassembled into lines there
  and handed to a queue; everything else — parsing, app state, LVGL — happens in
  `loop()`. Keep it that way.
- Notification text is UTF-8 and may contain emoji, which the built-in Montserrat
  fonts cannot render. Gadgetbridge's per-device "transliterate" setting is the
  cheap fix.
