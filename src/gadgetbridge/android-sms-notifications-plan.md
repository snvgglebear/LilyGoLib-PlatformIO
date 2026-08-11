# Plan: SMS and text messaging, Gadgetbridge side

Companion to the firmware in this directory, which now threads SMS and chat
notifications into conversations and can reply to them. This is the Android half:
what already works, what has to change, and which protocol additions the two
sides need to agree on.

**Target:** the `twatch_ultra` branch of
<https://codeberg.org/snvgglebear/Gadgetbridge>, files:

- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraCoordinator.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/devices/twatch_ultra/TWatchUltraConstants.java`
- `app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/twatch_ultra/TWatchUltraDeviceSupport.java`

---

## 1. What already works (verified against that branch, 2026-08-10)

Good news first: **SMS already reaches the watch.** `onNotification` sends every
field the firmware needs, and null-valued fields drop out on their own because
`JSONObject.put(String, Object)` removes the key rather than storing a JSON null
— which is exactly the "omit, never send null" rule in §5 of the protocol doc.

```java
@Override
public void onNotification(final NotificationSpec notificationSpec) {
    final JSONObject o = new JSONObject();
    o.put("t", "notify");
    o.put("id", notificationSpec.getId());
    o.put("src", notificationSpec.sourceName);
    o.put("title", notificationSpec.title);
    o.put("subject", notificationSpec.subject);
    o.put("body", notificationSpec.body);
    o.put("sender", notificationSpec.sender);
    o.put("tel", notificationSpec.phoneNumber);
    uartTxJSON("onNotification", o);
}
```

For an SMS, Gadgetbridge core fills in `phoneNumber`, and `sender` when the
number resolves to a contact. That pair is what the firmware keys on: `tel`
identifies the thread, `sender` names it, and `tel` is what makes a reply go out
as an SMS rather than through the notification's own reply action.

The action path back is wired up too, and dispatches properly:

```java
case "notify": {
    final GBDeviceEventNotificationControl notificationControl = new GBDeviceEventNotificationControl();
    notificationControl.event = enumValue(GBDeviceEventNotificationControl.Event.class,
                                          json.getString("n"), Event.UNKNOWN);
    if (json.has("id"))  notificationControl.handle = json.getInt("id");
    if (json.has("tel")) notificationControl.phoneNumber = json.getString("tel");
    if (json.has("msg")) notificationControl.reply = json.getString("msg");
    evaluateGBDeviceEvent(notificationControl);
}
```

Two things worth knowing about that:

- `enumValue` does `Enum.valueOf(type, name.toUpperCase(Locale.ROOT))`, so the
  lowercase `n` values the protocol specifies (`dismiss`, `dismiss_all`, `open`,
  `mute`, `reply`) match the uppercase enum constants. No change needed.
- `GBDeviceEventNotificationControl.evaluate()` branches on `phoneNumber`: when
  it is set it calls `SmsManager.getDefault().sendTextMessage(phoneNumber, null,
  reply, null, null)` and returns; otherwise it broadcasts to
  `NotificationListener` with `handle`/`title`/`reply` extras, which fires the
  notification's own RemoteInput action. Both branches are what we want, and the
  watch already picks between them by including `tel` or not.

So the messaging feature is not blocked on Android. What follows is about making
it *reliable* and removing the guesswork the firmware currently has to do.

## 2. Changes

### 2.1 Confirm the SEND_SMS runtime grant (do this first)

The SMS branch above throws `SecurityException` without `android.permission.SEND_SMS`,
and the failure is silent from the watch's point of view — it shows the reply in
the thread either way. Before writing any code: check the manifest declares it,
and that the permission is actually granted to the installed build (Settings →
Apps → Gadgetbridge → Permissions). If it is missing, replying to an SMS from
the watch will appear to work and nothing will be sent.

Worth adding regardless: log the outcome, and consider a `{"t":"warn","msg":…}`
toast back to the watch on failure (§6.7 in the other direction is watch→phone,
so this would be a new phone→watch toast — see §3.4 if you want it).

### 2.2 Canned replies from the phone's list

The watch currently ships three hardcoded replies (`GB_QUICK_REPLIES` in
`gb_ui.cpp`) because Gadgetbridge is not offering it any. Fix on the coordinator:

```java
@Override
public int getCannedRepliesSlotCount(@NonNull final GBDevice device) {
    return 16;   // same as BangleJSCoordinator
}
```

and add the settings screens to `getSupportedDeviceSpecificSettings()`, which
today lists only `devicesettings_transliteration` and
`devicesettings_autoremove_notifications`:

```java
R.xml.devicesettings_header_notifications,
R.xml.devicesettings_send_app_notifications,
R.xml.devicesettings_canned_reply_16,
R.xml.devicesettings_transliteration,
R.xml.devicesettings_autoremove_notifications,
```

(Resource names taken from `BangleJSCoordinator`, which uses all of these.)

That gives the user a canned-replies editor and populates
`NotificationSpec.cannedReplies`. Then push the list to the watch so the reply
screen offers the user's own replies — a new message type, §3.3 below — sent
from `initializeDevice` and again from `onSendConfiguration` when the preference
changes.

### 2.3 Tell the watch what kind of notification it is

`NotificationSpec.type` is the field that says "this is an SMS" / "this is a
chat message", and it is currently not sent. Without it the firmware falls back
to matching app names (`isMessagingApp()` in `gb_messages.cpp`), which is a
guess that will always be wrong for somebody's messenger.

```java
if (notificationSpec.type != null) {
    o.put("type", notificationSpec.type.name().toLowerCase(Locale.ROOT));  // "generic_sms", …
}
```

This is the highest-value change in this document: it makes classification
exact, and the heuristic becomes a fallback for old phone builds only.

### 2.4 Say whether a reply is actually possible

The watch offers Reply on every message. When the notification has neither a
phone number nor a reply action, that reply goes nowhere and the user is not
told. Send the capability:

```java
boolean canReply = notificationSpec.phoneNumber != null;
if (!canReply && notificationSpec.attachedActions != null) {
    for (final NotificationSpec.Action action : notificationSpec.attachedActions) {
        if (action.isReply()) { canReply = true; break; }
    }
}
if (canReply) o.put("reply", true);
```

The firmware then hides the Reply button rather than lying about it.

### 2.5 A stable thread key for group chats

The firmware threads on `src` + (`tel` ?: `sender` ?: `title`). In a group chat
`sender` changes per message while Android keeps one notification, so messages
from different people either collapse oddly or split into threads that should be
one. `NotificationSpec.key` (Android's notification key) is stable per
conversation:

```java
if (notificationSpec.key != null) o.put("key", notificationSpec.key);
```

Keys can be long; hash or truncate if they push the line size up. The firmware
prefers `key` when present and keeps the current rule when absent.

### 2.6 Keep bodies watch-sized

The watch discards any line over 8192 bytes (§2) and keeps 12 messages per
thread. A long email body sent as a notification can approach that, and every
line is written in `UART_CHUNK_SIZE` pieces, so a huge body is also slow.
Truncate on the phone:

```java
o.put("body", StringUtils.truncate(notificationSpec.body, 512));
```

512 characters is roughly four lines on the Ultra's panel and comfortably inside
every limit on both sides.

## 3. Protocol additions

All additive, all optional, all safe under §10 of the protocol doc — an older
watch ignores unknown fields, and this firmware treats every one of them as
absent-by-default. **`.claude/twatch-ultra-ble-protocol.md` must be updated in
the same change**, since that document is the contract.

### 3.1 `notify` gains three fields (§5.3)

| Field | Type | Meaning |
| --- | --- | --- |
| `type` | string | `NotificationType` lowercased, e.g. `generic_sms`, `conversation`, `generic_email` |
| `reply` | bool | present and true when replying will actually reach somebody |
| `key` | string | stable per-conversation key; threads group on it when present |

```json
{"t":"notify","id":8231,"src":"Messages","title":"Ada Lovelace","body":"On my way",
 "sender":"Ada Lovelace","tel":"+15551234567","type":"generic_sms","reply":true,"key":"0|com.android.messaging|42|null|10123"}
```

### 3.2 Firmware classification rule, once `type` exists

1. `type` present → messages are `generic_sms`, `generic_mms`, `conversation`,
   and anything a messaging app sends; everything else is an alert.
2. `type` absent → today's rule: `tel` or `sender` set, else the app-name list.

### 3.3 New: `replies` — push the canned replies (phone → watch)

| Field | Type | Meaning |
| --- | --- | --- |
| `d` | array of strings | replies to offer, in order; empty clears back to the built-ins |

```json
{"t":"replies","d":["OK","On my way","Call you later","Can't talk right now"]}
```

Sent at connect and whenever the preference changes. The watch caps the list
(sixteen is plenty) and falls back to its built-in three if it never arrives.

### 3.4 Optional: `toast` (phone → watch)

If §2.1's failure reporting is wanted, the mirror of §6.7:
`{"t":"toast","msg":"SMS permission denied"}`. Only worth adding if the silent
failure proves annoying in practice.

## 4. Order of work

1. **§2.1** permission check — no code, and it decides whether replies work at all.
2. **§2.3** `type` — smallest change, biggest correctness win.
3. **§2.2** canned replies: coordinator slots + settings, then `replies` (§3.3).
4. **§2.4** `reply` flag, **§2.5** `key`, **§2.6** truncation.
5. Update `.claude/twatch-ultra-ble-protocol.md` with §3.
6. Firmware follow-ups, once the phone sends the new fields: consume `type` and
   `key` in `gb_messages.cpp`, `reply` in `gb_ui.cpp`, `replies` in `gb_app.cpp`.

Steps 1–2 are worth doing on their own; the rest is polish that can land later
without breaking either side.

## 5. Test matrix

| Case | Expected on the watch |
| --- | --- |
| SMS from a saved contact | thread named for the contact, phone icon, Reply works |
| SMS from an unknown number | thread named by number (`sender` absent) |
| Two SMS from the same number | one thread, two bubbles, newest last |
| Same notification updated in place (typing indicator, edit) | text replaced, not duplicated |
| Signal/Telegram message | thread under that app, envelope icon |
| Group chat, several senders | one thread once `key` (§2.5) lands; watch for splitting before that |
| Non-message notification (app update) | stays in **Alerts**, never in **Chats** |
| Reply to SMS | arrives as a text; check the recipient's phone, not just the watch |
| Reply to a chat app | goes through RemoteInput; fails silently if the app exposes no reply action — the `reply` flag (§2.4) is what prevents offering it |
| Dismiss on phone | thread's message disappears from the watch (`notify-`) |
| Dismiss on watch | notification clears on the phone |
| "Dismiss all" from Alerts | both lists empty |
| Emoji in a message | boxes unless transliteration is on — expected, see the README |
| 4 kB body | truncated per §2.6; without it the watch drops the line entirely |

## 6. Risks

- **Notification id stability.** The watch replies and dismisses using the id
  from the newest message in a thread. If Android recycles or renumbers ids, a
  reply can land on the wrong notification. `key` (§2.5) is the more stable
  handle if this bites, though `GBDeviceEventNotificationControl.handle` is a
  `long` id, so the id stays the reply route regardless.
- **Silent reply failure.** Covered in §2.1/§2.4; the watch cannot tell the
  difference between "sent" and "swallowed" without help from the phone.
- **Dismiss loops.** `devicesettings_autoremove_notifications` plus a watch-side
  dismiss both remove the notification; make sure the resulting `notify-` echo
  is harmless (it is today — removing an unknown id is a no-op on the watch).
- **Thread eviction.** The watch keeps 8 conversations and 12 messages each.
  It is a watch, not an archive; the phone remains the source of truth.
