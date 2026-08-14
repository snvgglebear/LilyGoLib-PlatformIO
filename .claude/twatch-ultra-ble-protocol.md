# T-Watch Ultra ⇄ Gadgetbridge BLE Protocol

Companion protocol for the LilyGO T-Watch Ultra (ESP32-S3). This is the contract the
Gadgetbridge `TWATCH_ULTRA` device support implements; firmware that follows this document
will pair, sync, and receive notifications without any changes on the Android side.

The board has no stock companion protocol, so this one is *defined* rather than reverse
engineered. The transport is the Nordic UART Service, matching the firmware stacks already in
circulation for this board, and the message shapes mirror the Bangle.js / wasp-os "GB" protocol
that Gadgetbridge has spoken for years — so anything unclear here can usually be resolved by
looking at how those watches do it.

**Gadgetbridge side:** `devices/twatch_ultra/TWatchUltraConstants.java`,
`devices/twatch_ultra/TWatchUltraCoordinator.java`,
`service/devices/twatch_ultra/TWatchUltraDeviceSupport.java`

---

## 1. Transport

The watch is the GATT **server**; the phone connects as client.

| Role | UUID | Properties | Direction |
| --- | --- | --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | — | Nordic UART Service |
| RX characteristic | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | `WRITE`, `WRITE_NR` | phone → watch |
| TX characteristic | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | `NOTIFY` (+ CCCD `0x2902`) | watch → phone |

Names are from the **watch's** point of view: the phone writes to RX and subscribes to TX.

Two standard services are optional but recommended. Gadgetbridge reads both at connect time and
falls back to the UART if they are absent:

| Service | Used for |
| --- | --- |
| Device Information `0x180A` | Firmware Revision `0x2A26` (falls back to Software Revision `0x2A28`), Hardware Revision `0x2A27` |
| Battery Service `0x180F` | Battery Level `0x2A19`; subscribed for notifications if the characteristic supports it |

Implement Battery Service *or* send `status` messages (§6.2) — either populates the battery
display. Implementing both is fine; whichever arrives last wins.

## 2. Framing

Every message is **one JSON object on one line, UTF-8, terminated by `\n`**.

Rules the phone follows, which your firmware can rely on:

- Outgoing writes are split into **20-byte chunks** regardless of the negotiated MTU. A single
  JSON object will usually arrive across several writes. **You must buffer until you see `\n`.**
- Nothing else is added — no length prefix, no framing bytes, no wrapper function call.

Rules your firmware should follow, which the phone already handles:

- Notify in whatever chunk size fits your MTU. The phone reassembles until `\n`.
- Each line is trimmed before parsing, so a trailing `\r` is harmless — `\r\n` works.
- Empty lines are skipped.
- **Lines that do not start with `{` are ignored.** You can print boot banners and log output on
  the same characteristic without confusing the phone.
- Unknown `t` values are logged and dropped. Both sides must tolerate message types they do not
  know — that is what makes this protocol extensible.
- Malformed JSON drops that line only; the parser does not desync.
- Lines longer than **8192 bytes** are discarded. Keep messages well under that.

## 3. Advertising and device name

Gadgetbridge matches on the **advertised** name — the Complete Local Name (AD type `0x09`) or
Shortened Local Name (`0x08`) in the advertising payload or scan response. This is what your
firmware sets, so you control it entirely.

The name must match this regular expression, case-insensitively:

```
^T[-_ ]?Watch[-_ ]?(S3[-_ ]?)?Ultra.*$
```

| Name | Matches |
| --- | --- |
| `T-Watch Ultra` | yes |
| `T-Watch Ultra 4F2A` | yes |
| `TWatch Ultra` | yes |
| `T-Watch-S3-Ultra` | yes |
| `t_watch_ultra` | yes |
| `T-Watch S3` | no — no `Ultra` |
| `PipBoy-4F2A` | no |

**Recommended:** `T-Watch Ultra XXXX`, where `XXXX` is the last two bytes of the BLE MAC in hex.
The trailing `.*` in the pattern is there for exactly this, and a per-unit suffix keeps two
watches distinguishable in the scan list.

Payload budget, if you advertise the service UUID too: the advertising packet is 31 bytes. Flags
take 3, and `T-Watch Ultra 4F2A` as a Complete Local Name takes 20 (18 characters + 2 header),
leaving 8 — not enough for a 128-bit UUID, which needs 18. **Put the NUS UUID in the scan
response**, or advertise only the name. Gadgetbridge does not filter on the service UUID for this
device, so advertising it is good practice rather than a requirement.

Set the GAP Device Name characteristic (`0x2A00`) to the same string. It is not what gets matched,
but Android reads it after connecting and a mismatch is confusing to debug.

> **Android caches device names.** After you change the advertised name, the phone may keep
> showing the old one indefinitely. Forget the device in Android's Bluetooth settings and toggle
> Bluetooth off and on before concluding your change did not work.

## 4. Bonding

Gadgetbridge asks the user whether to bond (`BONDING_STYLE_ASK`), because whether pairing is
required depends on how the firmware was built. Both work:

- **No bonding** — simplest, and fine for a device on your own bench.
- **Bonding with a passkey** — set `BLE_SM_PAIR_AUTHREQ_BOND` and a static or displayed passkey.
  Android drives the pairing dialog; nothing protocol-level changes.

There is no application-layer handshake, authentication, or encryption in this protocol. BLE
bonding is the only security boundary. Do not carry secrets over it without adding your own layer.

## 5. Messages: phone → watch

Written to the RX characteristic.

Fields whose value is unavailable are **omitted entirely**, not sent as `null` — Android's JSON
library drops null-valued keys. Treat every field except `t` as optional and code defensively.

### 5.1 `ver` — request versions

Sent once during connection setup. Reply with a `ver` message (§6.1).

```json
{"t":"ver"}
```

### 5.2 `time` — set clock

Sent during connection setup when the user has "sync time" enabled, and whenever Android's time or
timezone changes.

| Field | Type | Meaning |
| --- | --- | --- |
| `ts` | int | Unix epoch, **seconds**, UTC |
| `o` | int | Offset from UTC in **minutes**, DST included |

Local time is `ts + o * 60`. The offset is signed and can be non-hourly (India is `330`).

```json
{"t":"time","ts":1786060800,"o":-300}
```

### 5.3 `notify` — incoming notification

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | int | Handle for this notification; needed for `notify-` and for replies |
| `src` | string | Source app name, e.g. `Signal` |
| `title` | string | Notification title |
| `subject` | string | Subject line, mostly email |
| `body` | string | Body text |
| `sender` | string | Sender name |
| `tel` | string | Sender phone number |

```json
{"t":"notify","id":8231,"src":"Signal","title":"Ada Lovelace","body":"On my way","sender":"Ada Lovelace"}
```

Text is transliterated to ASCII first if the user enables that in device settings; otherwise it is
UTF-8 and may contain any character, including emoji.

### 5.4 `notify-` — dismiss notification

The notification was dismissed on the phone. Remove it from the watch UI.

```json
{"t":"notify-","id":8231}
```

### 5.5 `call` — call state

| Field | Type | Meaning |
| --- | --- | --- |
| `cmd` | string | `undefined`, `accept`, `incoming`, `outgoing`, `reject`, `start`, `end` |
| `name` | string | Caller name, if resolved from contacts |
| `number` | string | Caller number |

```json
{"t":"call","cmd":"incoming","name":"Grace Hopper","number":"+15551234567"}
```

`incoming` should raise the call screen; `start` means answered; `end`, `reject` and `accept`
should dismiss it.

### 5.6 `musicinfo` — track metadata

| Field | Type | Meaning |
| --- | --- | --- |
| `artist` | string | |
| `album` | string | |
| `track` | string | Track title |
| `dur` | int | Duration in **seconds**; `-1` if unknown |
| `c` | int | Total tracks in queue; `-1` if unknown |
| `n` | int | Index of current track; `-1` if unknown |

```json
{"t":"musicinfo","artist":"Boards of Canada","album":"Geogaddi","track":"Dawn Chorus","dur":257,"c":23,"n":9}
```

### 5.7 `musicstate` — playback state

| Field | Type | Meaning |
| --- | --- | --- |
| `state` | string | `play`, `pause`, `stop`, or `unknown` |
| `position` | int | Playback position in **seconds**; `-1` if unknown |
| `shuffle` | int | `1` enabled, `0` disabled, `-1` unknown |
| `repeat` | int | `1` enabled, `0` disabled, `-1` unknown |

```json
{"t":"musicstate","state":"play","position":42,"shuffle":0,"repeat":-1}
```

### 5.8 `alarm` — set alarms

**Replaces the full set** every time. Only enabled alarms are sent, so an empty `d` array means
"clear all alarms". Gadgetbridge offers 8 slots for this device.

| Field | Type | Meaning |
| --- | --- | --- |
| `d` | array | Alarm objects, may be empty |
| `d[].h` | int | Hour, 0–23 |
| `d[].m` | int | Minute, 0–59 |
| `d[].rep` | int | Weekday bitmask; `0` means fire once |

Bitmask: Mon `1`, Tue `2`, Wed `4`, Thu `8`, Fri `16`, Sat `32`, Sun `64`. So weekdays are `31`,
every day is `127`.

```json
{"t":"alarm","d":[{"h":7,"m":30,"rep":31},{"h":9,"m":0,"rep":0}]}
```

### 5.9 `weather` — current conditions

| Field | Type | Meaning |
| --- | --- | --- |
| `temp` | int | Current temperature in **Kelvin** |
| `hum` | int | Relative humidity, percent |
| `code` | int | OpenWeatherMap condition ID, e.g. `800` clear |
| `txt` | string | Human-readable condition |
| `wind` | float | Wind speed in **km/h** |
| `wdir` | int | Wind direction in degrees, meteorological |
| `loc` | string | Location name |

Kelvin because that is Gadgetbridge's internal unit. Convert on the watch: `°C = temp - 273.15`,
`°F = (temp - 273.15) × 9/5 + 32`.

```json
{"t":"weather","temp":291,"hum":64,"code":803,"txt":"broken clouds","wind":11.2,"wdir":240,"loc":"Bristol"}
```

### 5.10 `find` — find device

The user pressed "find device" in Gadgetbridge. Start ringing or vibrating until told to stop.

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | bool | `true` start, `false` stop |

```json
{"t":"find","n":true}
```

### 5.11 `vibrate` — constant vibration

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | int | Intensity |

```json
{"t":"vibrate","n":60}
```

## 6. Messages: watch → phone

Notified on the TX characteristic. All are optional — send only what your firmware supports.

### 6.1 `ver` — report versions

Send in response to `{"t":"ver"}`. Populates the version fields on the device card.

| Field | Type | Meaning |
| --- | --- | --- |
| `fw` | string | Firmware version; defaults to `N/A` if absent |
| `hw` | string | Hardware revision; defaults to `T-Watch Ultra` if absent |

```json
{"t":"ver","fw":"0.4.1","hw":"T-Watch Ultra"}
```

### 6.2 `status` — battery

Send on connect and whenever the level changes by a percent or so. Do not send it every second.

| Field | Type | Meaning |
| --- | --- | --- |
| `bat` | int | Charge percent; clamped to 0–100 by the phone |
| `volt` | float | Battery voltage, volts |
| `chg` | bool | `true` while charging; drives the charging indicator |

```json
{"t":"status","bat":78,"volt":3.94,"chg":false}
```

### 6.3 `findPhone` — ring the phone

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | bool | `true` start, `false` stop; defaults to `true` if absent |

```json
{"t":"findPhone","n":true}
```

### 6.4 `music` — media control

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | string | `play`, `pause`, `playpause`, `next`, `previous`, `volumeup`, `volumedown`, `forward`, `rewind` |

Matched case-insensitively; unknown values are ignored.

```json
{"t":"music","n":"next"}
```

### 6.5 `call` — call control

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | string | `accept`, `end`, `reject`, `ignore`, `incoming`, `outgoing`, `start` |

In practice a watch sends `accept`, `end`, `reject` or `ignore`.

```json
{"t":"call","n":"accept"}
```

### 6.6 `notify` — notification action

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | string | `dismiss`, `dismiss_all`, `open`, `mute`, `reply` |
| `id` | int | The `id` from the `notify` message being acted on |
| `tel` | string | Phone number, for `reply` to an SMS |
| `msg` | string | Reply text, for `reply` |

```json
{"t":"notify","n":"dismiss","id":8231}
{"t":"notify","n":"reply","id":8231,"tel":"+15551234567","msg":"Running late"}
```

### 6.7 `info` / `warn` / `error` — toast on the phone

Shows an Android toast. Useful during bring-up; do not use it as a logging channel.

| Field | Type | Meaning |
| --- | --- | --- |
| `msg` | string | Text to display |

```json
{"t":"warn","msg":"GNSS fix lost"}
```

## 7. Connection sequence

What Gadgetbridge does, in order, once the GATT connection is up:

1. Discovers services and subscribes to the UART TX characteristic.
2. Reads Device Information (`0x180A`) if present.
3. Reads Battery Level (`0x180F`) and subscribes to it if present.
4. Sends `{"t":"ver"}`.
5. Sends `{"t":"time",...}`, if the user has time sync enabled.
6. Marks the device initialized.

Steps 4 and 5 are queued immediately after subscription, so **be ready to receive as soon as the
CCCD is written**. If your firmware needs setup time after connecting, buffer incoming lines
rather than dropping them.

After that, messages are event-driven in both directions. Nothing polls, and there is no
keep-alive — Gadgetbridge reconnects automatically if the link drops.

## 8. Firmware skeleton

Illustrative NimBLE-Arduino 2.x sketch showing the framing, not a complete implementation. It has
not been compiled — treat the structure as the point, not the API details.

```cpp
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

static const char* NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_RX      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // phone writes here
static const char* NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // we notify here

static NimBLECharacteristic* txChar = nullptr;
static String rxBuffer;

static void handleMessage(const JsonDocument& doc) {
  const char* t = doc["t"] | "";

  if (!strcmp(t, "ver")) {
    sendJson("{\"t\":\"ver\",\"fw\":\"" FW_VERSION "\",\"hw\":\"T-Watch Ultra\"}");
  } else if (!strcmp(t, "time")) {
    time_t utc    = doc["ts"] | 0;
    int    offset = doc["o"]  | 0;          // minutes
    setLocalTime(utc + offset * 60);
  } else if (!strcmp(t, "notify")) {
    ui.pushNotification(doc["id"] | 0,
                        doc["title"]  | "",
                        doc["body"]   | "",
                        doc["src"]    | "");
  } else if (!strcmp(t, "notify-")) {
    ui.dismissNotification(doc["id"] | 0);
  } else if (!strcmp(t, "call")) {
    ui.setCallState(doc["cmd"] | "undefined", doc["name"] | "", doc["number"] | "");
  } else if (!strcmp(t, "find")) {
    haptics.setAlarm(doc["n"] | false);
  }
  // Unknown types are ignored on purpose — that is what keeps this extensible.
}

// Reassemble 20-byte writes into whole lines.
class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    rxBuffer += c->getValue().c_str();
    if (rxBuffer.length() > 8192) { rxBuffer = ""; return; }

    int nl;
    while ((nl = rxBuffer.indexOf('\n')) >= 0) {
      String line = rxBuffer.substring(0, nl);
      rxBuffer.remove(0, nl + 1);
      line.trim();
      if (line.isEmpty() || line[0] != '{') continue;

      JsonDocument doc;
      if (!deserializeJson(doc, line)) handleMessage(doc);
    }
  }
};

// Notify a single line. Chunk to the negotiated MTU; the phone reassembles.
void sendJson(const String& json) {
  if (!txChar) return;
  String line = json + "\n";
  const size_t chunk = NimBLEDevice::getMTU() - 3;
  for (size_t i = 0; i < line.length(); i += chunk) {
    txChar->setValue((uint8_t*)line.c_str() + i, min(chunk, line.length() - i));
    txChar->notify();
  }
}

void setupBle() {
  char name[24];
  const uint8_t* mac = NimBLEDevice::getAddress().getBase()->val;
  snprintf(name, sizeof(name), "T-Watch Ultra %02X%02X", mac[1], mac[0]);
  NimBLEDevice::init(name);

  NimBLEServer*  server  = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(NUS_SERVICE);

  NimBLECharacteristic* rxChar = service->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  txChar = service->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(name);                        // this is what Gadgetbridge matches
  adv->addServiceUUID(NUS_SERVICE);          // goes in the scan response
  adv->enableScanResponse(true);
  adv->start();
}
```

## 9. Conformance checklist

Minimum for a watch that shows up and works:

- [ ] Advertises a name matching `^T[-_ ]?Watch[-_ ]?(S3[-_ ]?)?Ultra.*$`
- [ ] Exposes the NUS service with RX writable and TX notifiable
- [ ] Buffers incoming writes until `\n` before parsing
- [ ] Terminates every outgoing message with `\n`
- [ ] Ignores unknown `t` values instead of erroring
- [ ] Handles `time` and `notify`

Worth adding next, in rough order of payoff: `status` or Battery Service, `ver`, `notify-`,
`call`, `find`, then music and weather.

## 10. Extending it

Add message types freely — both sides ignore what they do not recognise, so a watch that sends
`{"t":"steps","n":8412}` to a Gadgetbridge build that predates step support loses nothing.

Two things to keep in mind if you plan to upstream the device support:

- **Activity data is not covered here.** Recording steps, heart rate or sleep into Gadgetbridge's
  database needs a schema entity and a sample provider on the Android side, not just a message
  type. Worth designing the message format and the schema together rather than bolting one on.
- **Keep `t` values stable.** They are the wire contract. Adding fields to an existing message is
  backwards compatible; renaming or repurposing one is not.
