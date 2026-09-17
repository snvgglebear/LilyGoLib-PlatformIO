# Using `type` and `class` in the firmware notification handler

## What the phone sends

When a notification arrives, Gadgetbridge appends two extra fields (only when the type is known):

```json
{"t":"notify","id":42,"src":"Signal","title":"Ada","body":"On my way",
 "type":"signal_single_conversation","class":"generic_communication_app"}
```

- **`type`** — the exact `NotificationType` enum name, lowercased (e.g. `"generic_sms"`, `"gmail"`, `"facebook_messenger"`, `"signal_single_conversation"`)
- **`class`** — the coarse bucket from `getGenericType()` (e.g. `"generic_notif"`, `"generic_sms"`, `"generic_email"`, `"generic_communication_app"`, `"generic_phone"`)

`class` is the reliable signal for branching logic; `type` is available for finer control.

---

## Step 1 — Add the fields to `GbNotification`

**File:** `gadgetbridge_ble/gb_protocol.h`, inside `struct GbNotification`

```cpp
struct GbNotification {
    int32_t id = 0;
    std::string src;
    std::string title;
    std::string subject;
    std::string body;
    std::string sender;
    std::string tel;
    std::string type;   // add
    std::string cls;    // add ("class" is a C++ keyword, so use cls)
};
```

---

## Step 2 — Parse them in the protocol layer

**File:** `gadgetbridge_ble/gb_protocol.cpp`, in the `"notify"` branch of `gb_protocol_dispatch()` (around lines 116–125)

```cpp
notif.type = doc["type"] | "";
notif.cls  = doc["class"] | "";
```

Add these two lines immediately after the existing `notif.tel = doc["tel"] | "";` line.

---

## Step 3 — Use `cls` to replace the messaging-app allowlist

**File:** `gadgetbridge_ble/gb_messages.cpp`

`isTextMessage()` currently classifies a notification as a message if `tel` is set, `sender` is set, or `src` matches a hardcoded list of app names. Replace the `src`-matching branch with a `cls` check so that any app Gadgetbridge considers a communication app is automatically threaded as a message, without the firmware needing its own app list.

Change `isTextMessage()` from:

```cpp
bool GbMessageStore::isTextMessage(const GbNotification& n) {
    if (!n.tel.empty()) return true;
    if (!n.sender.empty()) return true;
    // ... long src-matching block ...
}
```

To:

```cpp
bool GbMessageStore::isTextMessage(const GbNotification& n) {
    if (!n.tel.empty()) return true;
    if (!n.sender.empty()) return true;
    if (!n.cls.empty()) {
        // Trust Gadgetbridge's own classification when available.
        return n.cls == "generic_sms"
            || n.cls == "generic_communication_app";
    }
    // Fallback for older Gadgetbridge builds that don't send class.
    // ... existing src-matching block ...
}
```

This preserves backward compatibility: if `cls` is empty (older Gadgetbridge build or a notification type that has no generic type), the existing src-matching code still runs.

---

## Step 4 — Use `type` for app-specific icon or colour (optional)

If the UI renders a per-source icon or accent colour, `type` lets you do this without matching `src` strings. Example in the notification list renderer (wherever `summarise()` or the icon is chosen):

```cpp
static lv_color_t accentFor(const GbNotification& n) {
    if (n.cls == "generic_sms")                    return lv_color_hex(0x4caf50);
    if (n.cls == "generic_email")                  return lv_color_hex(0xf44336);
    if (n.type == "signal_single_conversation"
     || n.type == "signal_group_conversation")     return lv_color_hex(0x2090ea);
    if (n.type == "whatsapp")                      return lv_color_hex(0x25d366);
    return lv_color_hex(0x9e9e9e); // default grey
}
```

Call this wherever the notification list item colour is currently hard-coded.

---

## Step 5 — Verify with the stdio emulator

The `gb_link_stdio` link lets you feed JSON on stdin. Test the new fields directly:

```json
{"t":"notify","id":1,"src":"Gmail","title":"Invoice","body":"Your invoice is ready","type":"gmail","class":"generic_email"}
{"t":"notify","id":2,"src":"Signal","sender":"Ada","body":"On my way","type":"signal_single_conversation","class":"generic_communication_app"}
{"t":"notify","id":3,"src":"SomeNewApp","sender":"Bob","body":"Hey","type":"somenewapp","class":"generic_communication_app"}
```

Check that id 1 goes into the alerts list, and ids 2 and 3 both thread as conversations — id 3 especially, since `"SomeNewApp"` would not have matched the old `src` allowlist.

---

## Summary of files changed

| File | Change |
|------|--------|
| `gb_protocol.h` | Add `type` and `cls` to `GbNotification` |
| `gb_protocol.cpp` | Parse `doc["type"]` and `doc["class"]` |
| `gb_messages.cpp` | Prefer `cls` check over the src-string allowlist |
| `gb_ui.cpp` (optional) | Use `type`/`cls` for icons or accent colours |
