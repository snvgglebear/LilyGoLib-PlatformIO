# Plan: real Meshtastic protocol interop for the watch

**Revision (2026-08-15):** the recommendation in §4 has been replaced per explicit
direction from the user: the watch is the Meshtastic **node** — the "bridge" between
the phone and the LoRa mesh — not a client that pairs with a separate physical node.
Phone-side management happens through the official Meshtastic app (or any other
standards-compliant Meshtastic BLE client), as its own separate app — Gadgetbridge is
not part of this feature at all. This supersedes the original §4's "(a) first, (b)
later" call; §1-§3 (background research) are unchanged and still accurate as of
2026-08-15. §4 onward is rewritten below. A new §10 adds the requested persisted LoRa
radio on/off setting.

**Revision (2026-08-17):** §4.2, §5.3, §8 and §9 risk 1 are corrected — the
"two concurrent BLE connections" concern was overstated for the case that actually
matters (both phone apps on one handset share a single link; see §4.2), and it
concealed a real constraint the plan had missed: the two 128-bit service UUIDs do not
both fit in the advertising payload (§5.3, §9 risk 8). §7 gains the one phone-side
integration route that genuinely exists, for the record, without changing this plan's
scope. Also added the same day: **§4.1a** (why the watch does not *have* to be a node
to send messages, and what the node role buys for its price) and **§11** (the
duty-cycle decision — how much of the time the watch is actually a mesh member —
promoted out of §10.5's closing footnote and inserted into §6 as step 3, since
§5.4's shape depends on it).

**Target:** the `new_interface` branch of this repo (`/workspaces/LilyGoLib-PlatformIO`),
`src_dir = src/new_interface`. Files that would be added or changed:

- New: `src/new_interface/meshtastic_node/` — a self-contained module (node state
  machine + a second BLE-peripheral GATT service + protobuf codec + routing/crypto),
  analogous in shape to `src/new_interface/gadgetbridge_ble/`.
- New: `src/new_interface/app_meshtastic.h` / `.cpp` — the UI-facing seam, mirroring
  `src/new_interface/app_gadgetbridge.h` / `.cpp`.
- New: `src/new_interface/ui_meshtastic.cpp` — the launchable app screen(s), registered
  in `ui_main.cpp` the same way `ui_radio.cpp`/`ui_walkie.cpp` are. Status/monitor only
  — see §5.5 for why full configuration UI is deliberately not duplicated here.
- Changed: `platformio.ini` — add a nanopb code-generation step and its library
  dependency to `env_arduino`/`env_emulator`.
- Changed: `src/new_interface/hal_interface.h`/`.cpp` — new persisted LoRa
  radio-enabled flag (§10), an additive change consumed by `hw_sx1262.cpp`,
  `ui_radio.cpp`, `ui_msgchat.cpp`, and the new node module alike.
- Changed: `src/new_interface/gadgetbridge_ble/gb_ble.cpp`'s neighborhood — the new
  Meshtastic GATT service is a second service on the *same* NimBLE peripheral
  Gadgetbridge already runs, not a new BLE role (§4.2).
- Out of scope, not deferred: any change to the Gadgetbridge Android fork
  (`TWatchUltraDeviceSupport.java` etc.). The user's own framing is explicit — the
  phone handles Meshtastic through a different app entirely — so there is nothing
  for the Gadgetbridge fork to do here. See §7.

No existing file's raw-PHY behaviour changes as a prerequisite for this plan, beyond
the shared radio on/off gate added in §10, which is additive
(`hw_get_lora_enabled()`/`hw_set_lora_enabled()`, checked by `ui_radio.cpp` and
`ui_msgchat.cpp` before they key up the radio, same as the new node module).
`hal_interface.h`'s raw-radio functions (`hw_set_radio_params()` etc.) and
`ui_msgchat.cpp`'s broadcast chat are otherwise left exactly as they are; §2 explains
why this plan does not fold Meshtastic into either of them.

This document is planning only. Nothing here has been implemented.

---

## 1. What already works (verified against this checkout, 2026-08-15)

**There is no Meshtastic code anywhere in this repo.** Confirmed by grep — no match
for `meshtastic`, `ToRadio`, `FromRadio`, `mesh.proto` anywhere under `src/`. What
exists instead:

- **Raw LoRa PHY control.** `src/new_interface/hal_interface.h` declares
  `hw_set_radio_params()`/`hw_get_radio_params()`/`hw_set_radio_tx()`/
  `hw_get_radio_rx()`/`hw_set_radio_listening()` against a `radio_params_t` struct
  (freq, bandwidth, coding rate, power, spreading factor, sync word, mode). The
  SX1262 back end (`src/factory/hw_sx1262.cpp`, byte-identical logic expected in
  `src/new_interface/hw_sx1262.cpp`) implements this directly on top of RadioLib's
  `SX1262` class — `radio.setFrequency()`, `setBandwidth()`, `setSpreadingFactor()`,
  `setCodingRate()`, `setSyncWord()`, `setOutputPower()`, `startTransmit()`/
  `startReceive()`. RadioLib is vendored at
  `.pio/libdeps/emulator_watch_ultra/RadioLib`, version **7.1.2**.
- **A from-scratch broadcast chat, not Meshtastic.**
  `src/new_interface/ui_msgchat.cpp` sends and receives plain-text packets over
  whatever `radio_params_t` the LoRa test app (`ui_radio.cpp`) last applied. Its own
  header is explicit about the scope: "Broadcast, unencrypted, and unaddressed...
  There is no acknowledgement, retry, or delivery guarantee." This is the "custom
  LoRa messaging scheme" the task brief distinguishes from real Meshtastic — it sits
  at the same PHY layer this plan would build on, but has no packet framing, no node
  identity, no routing, and no interop with any other implementation.
- **A working precedent for a decoupled, wire-protocol module:**
  `src/new_interface/gadgetbridge_ble/`, ported twice already (`src/gadgetbridge/` →
  `src/custom_interface/gadgetbridge_ble/` → here). Its shape, confirmed by reading
  `gb_app.h`/`gb_protocol.h`:
  - `gb_protocol.*` is the pure wire codec (framing + JSON-line marshalling), with
    **no board, BLE, or LVGL dependency** — it compiles for hardware and the
    native/SDL2 emulator alike.
  - `gb_app.*` (`GbApp : public GbProtocolHandler`) is the state machine: it consumes
    decoded phone→watch messages, holds state, and exposes accessors plus a single
    `Listener` callback (`GB_CHANGE_*` enum) so UI code can react without the
    protocol layer knowing LVGL exists.
  - `gb_ble.cpp` is the one Arduino-specific transport (NimBLE GATT server);
    `gb_link_stdio.cpp` is the native stand-in. `gb_link.h` is the seam between them.
  - `src/new_interface/app_gadgetbridge.h` is the *app-level* fan-out point: it owns
    the one `GbApp::Listener` and dispatches to N independently-registered UI
    listeners (`app_gb_add_listener()`), which is exactly the "decouple gadgetbridge
    from UI setup" requirement in `src/custom_interface/plan.md` line 26.
  - This is the model this plan follows for `meshtastic_node/`: pure codec, a
    state-machine class, a BLE-peripheral transport, and an `app_meshtastic.*` fan-out
    seam — structurally parallel to Gadgetbridge, but a **separate module with its
    own listener**, not a hook into `GbProtocolHandler`. §2.1 explains why the two
    must not merge.
- **`src/custom_interface/plan.md` line 3** is the origin of this task: "lora/meshtastic
  functionality (primarily managing the connection from the phone, but also
  supporting a bluetooth keyboard.)" — phone-managed connection lifecycle is the
  explicit ask. This revision satisfies it via a standard Meshtastic phone app rather
  than a Gadgetbridge extension; see §7.

## 2. Architectural decisions (read before §3–§5)

### 2.1 Meshtastic gets its own module — it must not become a Gadgetbridge message type

`.claude/twatch-ultra-ble-protocol.md` §10 sanctions extending the Gadgetbridge
protocol with new `t` values "freely," and that is the right tool for
watch-native features like steps or a new toast type. It is the wrong tool here,
for reasons specific to what Meshtastic actually is:

- **The wire format is not this protocol's to define.** Gadgetbridge's JSON-line
  protocol in `.claude/twatch-ultra-ble-protocol.md` is *defined by this project*
  (§0: "The board has no stock companion protocol, so this one is defined rather
  than reverse engineered"). Meshtastic's client API is the opposite: it is
  Meshtastic's own protobuf schema (`meshtastic/protobufs` on GitHub — `mesh.proto`,
  `admin.proto`, `channel.proto`, `portnums.proto`, `config.proto`, …), versioned and
  evolved by that upstream project, consumed unmodified by every Meshtastic client
  (Android app, iOS app, Web, CLI). Re-encoding `ToRadio`/`FromRadio` protobuf frames
  as JSON fields inside `{"t":"notify",...}`-style messages would mean maintaining a
  translation layer against an external, independently-versioned schema forever, and
  would make this watch unable to talk to a standard Meshtastic phone app without a
  Gadgetbridge fork in the loop translating for it — defeating "real protocol
  interop" and directly contradicting the user's own direction that a *different*
  app handles this.
- **The transport identity is the same GATT role as Gadgetbridge, but a second
  service, not a merged one.** Meshtastic's own BLE service UUID
  (`6ba1b218-15a8-461f-9fa8-5dcae273eafd`, confirmed by the current
  `meshtastic/firmware` BLE stack and documented at
  <https://meshtastic.org/docs/development/device/client-api/>) is what every
  Meshtastic-aware client scans for. Because the watch is now the Meshtastic *node*
  (§4), it is the GATT **server** for this service too — the same role Gadgetbridge
  already plays (protocol doc §1: "The watch is the GATT server; the phone connects
  as client"). That means this is **not** a second BLE role to build (contrast the
  withdrawn original plan, which had the watch as a BLE *central* pairing outward to
  someone else's node — see §4.2) — it is a second GATT service hosted by the same
  NimBLE peripheral. Still two independent things that must coexist correctly
  (§4.2, §9 risk 8), just a materially smaller integration problem than dual-role
  central+peripheral would have been.
- **Different consumer, different lifecycle.** Gadgetbridge messages are consumed by
  one phone app the watch is bonded to for general watch control. Meshtastic packets
  are consumed by (or routed through) a mesh of possibly-unfamiliar nodes with their
  own identities, channel encryption, and hop-based routing — concepts that have no
  Gadgetbridge equivalent and would corrupt that protocol's simplicity if forced in.
- **Phone-side management is explicitly a different app.** The user's direction
  removes any ambiguity §2.1 previously had to hedge on ("what should cross into
  Gadgetbridge JSON, if anything"): nothing should. The Meshtastic app *is* the
  phone-management UX — connection status, node list, channel/region configuration,
  messaging — all of it, over the GATT service in §5.3, with zero Gadgetbridge
  involvement. §7 covers the one small, genuinely optional exception (a cosmetic
  status mirror) and why it is not part of this plan.

### 2.2 RadioLib supplies the PHY primitives, not Meshtastic compatibility

Checked the vendored copy at `.pio/libdeps/emulator_watch_ultra/RadioLib` (v7.1.2):
`grep -ri meshtastic` and `find -iname '*mesh*'` both return nothing. RadioLib is a
general-purpose driver for the SX126x/SX127x/SX128x/LR11x0/etc. LoRa silicon — it
gives `hw_sx1262.cpp` the register-level calls (`setFrequency`, `setBandwidth`,
`setSpreadingFactor`, `setCodingRate`, `setSyncWord`) that Meshtastic's own firmware
*also* uses (Meshtastic's `firmware` repo itself is built on RadioLib for its radio
back ends), but it does not ship Meshtastic's modem-preset tables, channel/frequency
plan, packet framing, or crypto. Concretely, matching a real Meshtastic mesh at the
PHY level means this watch's radio must be configured with the *same* derived
values Meshtastic computes internally from a `modem_preset` (e.g. `LongFast`,
`MediumSlow`, `ShortTurbo`) and a `LoraConfig.region` (US915, EU868, ANZ433, …):
bandwidth, spreading factor, coding rate, and sync word all have Meshtastic-specific
defaults that do not match this repo's own defaults (`hw_get_radio_params()` in
`hw_sx1262.cpp` currently defaults to SF12/125 kHz/sync word `0xCD` — SF12 alone is
already off from Meshtastic's default `LongFast` preset, and `0xCD` is this repo's
own private convention, not Meshtastic's `0x2B`/public-mesh sync word). None of that
table exists in this repo yet; it has to be transcribed from Meshtastic's firmware
source (`RadioInterface.cpp`/`RegionInfo` tables) as its own work item (§5.4).

**Conclusion: RadioLib is necessary infrastructure (it is confirmed literally the
same library Meshtastic's firmware itself calls into) but supplies zero Meshtastic
protocol compatibility on its own.** Everything above the SPI/register layer —
frequency-plan tables, modem presets, packet structure, encryption, routing — is new
code, and since the watch is now the node itself (not a client deferring that work to
a paired node, per the withdrawn option (a)), **all of it is in scope**, not just the
PHY-adjacent parts. §4.1 enumerates this concretely.

## 3. What "real Meshtastic protocol interop" is, precisely

Verified against Meshtastic's public docs and protobuf repo
(<https://meshtastic.org/docs/development/device/client-api/>,
<https://github.com/meshtastic/protobufs>) as of 2026-08-15. This section is
role-agnostic — it describes the API a Meshtastic *node* exposes, which is exactly
what the watch now implements (§4), so no rewrite was needed here beyond this note:

- **Client API (BLE transport).** A Meshtastic node exposes GATT service
  `6ba1b218-15a8-461f-9fa8-5dcae273eafd` with three characteristics: **ToRadio**
  (`f75c76d2-129e-4dad-a1dd-7866124401e7`, write — client sends a length-delimited
  `ToRadio` protobuf), **FromRadio** (`2c55e69e-4993-11ed-b878-0242ac120002`, read —
  client reads one queued `FromRadio` protobuf per read, draining until empty), and
  **FromNum** (`ed9da18c-a800-4f66-a670-aa7547e34453`, notify — the node bumps a
  packet counter on this characteristic whenever new data is queued in FromRadio, so
  the client knows to go read it; this is the mirror image of Gadgetbridge's NUS TX
  characteristic, but signals "go pull," not "here is the payload"). `ToRadio`
  contains a `oneof` of: a `MeshPacket` to send, `want_config_id` (start the config
  handshake), `disconnect`, and a few admin variants. `FromRadio` contains a `oneof`
  of: a received/queued `MeshPacket`, `MyNodeInfo`, `NodeInfo` (one per known mesh
  member), `Config`/`ModuleConfig` sections, `Channel` entries, `config_complete_id`
  (handshake done), `log_record`, and more. The watch, as the node, is the GATT
  server implementing all three characteristics; the Meshtastic phone app is the
  GATT client, exactly as it would be against any standalone hardware node.
- **MeshPacket.** The over-the-air/over-the-wire unit: `from`/`to` node IDs
  (32-bit, derived from the node's radio MAC), `channel` (an index, not the raw
  PSK), `id`, `hop_limit`/`hop_start` (flood-routing TTL), `want_ack`, `priority`,
  and a payload that is either a plaintext `Data` submessage (portnum + bytes, only
  ever true node-to-client-over-BLE/serial) or `encrypted` raw bytes (the over-the-air
  form). `Data.portnum` (from `portnums.proto`) tags the payload's meaning —
  `TEXT_MESSAGE_APP` for a chat message, `POSITION_APP`, `NODEINFO_APP`,
  `TELEMETRY_APP`, `ADMIN_APP`, etc. Third-party/experimental use is meant to take a
  port in the 256+ private range rather than inventing new core ports.
- **Channel encryption.** Each channel entry (`channel.proto`) carries a PSK; the
  well-known "Default" channel uses a fixed single-byte PSK value (`0x01`, meaning
  "use Meshtastic's compiled-in default AES128 key") rather than a random key, which
  is why an out-of-the-box Meshtastic mesh is *not* actually private — anyone with
  stock firmware can decode it. Real channels use a random 16 or 32-byte PSK,
  AES128 or AES256 in CTR mode, keyed per-channel. Getting this wrong (wrong nonce
  construction, wrong key derivation) produces packets the rest of the mesh silently
  drops rather than an error the watch can detect on its own. **This is now the
  watch's own responsibility** — as the node, it does the en/decryption itself
  (§4.1 item 3), unlike the withdrawn client-only option which could leave this to a
  paired node.
- **Routing.** Managed flood routing: a node retransmits a packet it hears if
  `hop_limit > 0` and it hasn't seen that `(from, id)` pair recently, decrementing
  `hop_limit` each hop. **The watch now needs the full mesh-node version of this** —
  a dedupe cache and the hop/rebroadcast logic — since it is a mesh node, not a pure
  client sitting behind one (§4.1 item 4).
- **Regional frequency plans + modem presets.** `config.proto`'s `LoRaConfig`
  encodes a `region` enum (US, EU_868, EU_433, ANZ, CN, JP, …) and a
  `modem_preset` enum (`LONG_FAST`, `LONG_SLOW`, `LONG_MODERATE`, `MEDIUM_SLOW`,
  `MEDIUM_FAST`, `SHORT_SLOW`, `SHORT_FAST`, `SHORT_TURBO`, or a fully custom
  bandwidth/SF/CR); the firmware derives the actual center frequency from the
  region's band edge plus a channel-number-and-slot calculation, and the actual
  bandwidth/SF/CR from the preset. **The watch needs both tables reproduced exactly**
  (§4.1 item 2) — this is unavoidable now that it owns the LoRa radio as a mesh
  node, not something a pure BLE client could skip.
- **Toolchain: nanopb.** Meshtastic's own firmware compiles its `.proto` files with
  **nanopb** (<https://github.com/nanopb/nanopb>), a protobuf-C generator sized for
  microcontrollers (no STL/exceptions/dynamic allocation required, unlike
  `protobuf-c`/full C++ protobuf). This is confirmed both by Meshtastic's own build
  ("Protocol Buffer System... built on nanopb," per the firmware architecture docs)
  and by nanopb being the de facto standard for ESP32/Arduino protobuf work — it has
  a PlatformIO-friendly build story (a Python codegen step + a small runtime
  library) and is what this repo should use too, precisely so the wire encoding
  matches Meshtastic's own byte-for-byte rather than needing to be independently
  verified against it.

## 4. Scope: the watch is the Meshtastic node, exposed as a BLE peripheral

### 4.0 The decision

Per explicit user direction: the watch owns the LoRa radio, participates in the mesh
directly, and exposes the standard Meshtastic BLE client API (§3) as a GATT
**server** — exactly like a standalone physical Meshtastic node (a LilyGo/Heltec/RAK
puck) does. This is the "bridge" framing: the watch bridges the LoRa mesh to
whatever BLE client connects to it, same as any Meshtastic node bridges its mesh to
the phone app paired with it.

The phone does not get a Gadgetbridge feature for this. The user pairs the *official*
Meshtastic Android/iOS app (or any other standards-compliant Meshtastic BLE client)
directly to the watch, the same way they would pair it to a standalone node — that
app already implements the full node-configuration and messaging UX Meshtastic users
expect; duplicating any of it in Gadgetbridge or in this watch's own UI would be
redundant work maintained against a moving upstream target for no benefit. §7 covers
what (if anything) still touches Gadgetbridge.

This corresponds to "option (b)" in the pre-revision version of this plan, which had
recommended starting with a smaller "option (a)" (BLE-central client to a *separate*
node) first and deferring full node behavior indefinitely. That recommendation is
withdrawn: a client-to-a-separate-node watch is not a bridge, and does not match what
was asked for.

### 4.1 What this requires (full mesh-node scope, all in scope now)

1. **Nanopb toolchain (§5.1)**, compiling the full relevant schema — `mesh.proto`,
   `channel.proto`, `config.proto`, `module_config.proto`, `portnums.proto`,
   `admin.proto`. The Meshtastic app configures a node's region, channel PSK, and
   modem preset through `config.proto`/`channel.proto`/`admin.proto` over this same
   BLE link — a node that doesn't handle those can't be onboarded from the app the
   normal way, so this is not the reduced schema slice a pure client could get away
   with.
2. **Regional frequency-plan + modem-preset tables** (§5.4), transcribed from
   Meshtastic firmware source and kept in sync as upstream adds regions/presets.
3. **AES-CTR channel encryption/decryption** per packet, with correct nonce
   construction matching Meshtastic's own (`packet_id` + `from` node ID) — a
   mismatch here doesn't error, it silently produces mesh traffic no other node can
   read, a difficult-to-debug failure mode.
4. **Flood-routing implementation:** hop-limit decrement, a seen-packet dedupe cache
   sized against real mesh traffic volumes, rebroadcast timing/jitter to avoid
   collision storms.
5. **A live node database** (`NodeInfo` ingestion, position/telemetry handling if
   supported) and **node identity/key management** — Meshtastic nodes generate a
   persistent public/private identity; this needs the same, stored safely (§9 risk
   6 covers the security ceiling of storing it in NVS).
6. **Regulatory-safe defaults:** no TX until a region is explicitly selected,
   mirroring Meshtastic's own onboarding. §10's radio on/off setting is a strictly
   coarser, always-available version of the same fail-closed idea — §10.4 explains
   how the two relate.

**Estimated effort is unchanged from the pre-revision plan's option (b) figure:
realistically several weeks to a few months** of focused work to reach parity with a
real Meshtastic node's basic behavior, plus ongoing maintenance to track upstream
protocol/preset changes. This plan does not shrink that estimate — it removes the
smaller "(a) first" milestone that used to sit in front of it, since a client to a
separate node was never what was asked for.

### 4.1a Does the watch have to be a node just to send messages? — asked 2026-08-17

No. Sending into a Meshtastic mesh requires *a* node; it does not require the watch to
be that node. Recorded here because the answer is the sharpest way to see what §4.0's
decision actually buys, and what it costs.

In Meshtastic's own model, a **client** (the phone app, `meshtastic-python`, a web
client) does not own a radio. It attaches to a node over BLE/serial/TCP, and the node
does the transmitting on its behalf. Critically, **the client/node link carries
plaintext `Data`** (§3) — the node performs channel encryption, routing and hop
management itself. So a client-role watch would need:

- nanopb and the `ToRadio`/`FromRadio`/`MeshPacket` subset of the schema (§5.1, and a
  reduced §5.2) — **still required**;
- a BLE **central** role to connect out to a node — the one thing §4.2 says the
  node-role design avoids;

and would need *none* of: regional frequency-plan and modem-preset tables (§4.1 item
2), AES-CTR channel crypto (item 3), flood routing and dedupe (item 4), node identity
and key persistence (item 5), or regulatory region gating (item 6). That is the
majority of §4.1's scope and effectively all of its hard, silently-failing parts.

What the watch gives up by not being a node:

- **Its own presence on the mesh.** A client shares the node ID of whatever node it
  is attached to; messages sent from the watch appear to the mesh as coming from that
  node, not from the watch. Other operators do not see a "watch" in their node list.
- **Working alone.** A client is useless without a node in BLE range. The watch stops
  being a self-contained mesh device and becomes a second screen for one.
- **Relaying.** A client never carries anyone else's traffic. It is not an antenna
  for the mesh in any sense.

The user's goal, reaffirmed 2026-08-17, is a watch that is itself a mesh
node/antenna, so §4.0 stands and this subsection changes nothing in the plan. It is
here so the trade is on the record: the node role is chosen for presence,
independence and relaying, and its price is items 2-6 plus §11's duty-cycle problem.
A third option that requires no watch-side Meshtastic code at all — sending through
the phone's Meshtastic app via Gadgetbridge — is in §7.1.

### 4.2 What gets simpler: no BLE-central role needed

The pre-revision plan's top-flagged risk — "NimBLE central + peripheral concurrency
is unproven in this codebase" — no longer applies in that form. Because the watch is
the GATT *server* (not a client connecting out to someone else's node), the
Meshtastic service is a **second GATT service on the existing peripheral role**, not
a second BLE *role*. `gb_ble.cpp` already proves the peripheral pattern (NUS server);
the Meshtastic service (ToRadio/FromRadio/FromNum, §3) is a second service alongside
it, both hosted by the one NimBLE GATT server already running. No scanning, no
central-mode connection state machine, no bonding-as-client logic is needed anywhere
in this plan.

**Two apps on one phone are not two BLE connections.** An earlier draft of this
section treated "Gadgetbridge on one link, the Meshtastic app on another" as the
replacement top risk. That is wrong for the common case: Android's Bluetooth stack
keeps **one ACL/GATT connection per remote device** and multiplexes every app's
`BluetoothGatt` client over it, refcounted, so two apps on the same handset talking
to the same watch produce a single link. The watch sees one `onConnect` and one
connection handle (`s_conn_handle`, `gb_ble.cpp:117`), with both apps' ATT traffic
riding it. There is no radio-level contention to design around, and no second
connection to hold.

What *is* shared per link, and therefore is the actual coexistence surface:

- **MTU** — negotiated once per link; the first requester wins and a later request
  returns the already-negotiated value. `NimBLEDevice::setMTU(247)`
  (`gb_ble.cpp:179`) is generous enough for both protocols, so this needs no change,
  only awareness that the Meshtastic service cannot negotiate its own.
- **Connection interval** — one link, one interval. A latency- or
  throughput-sensitive request from either app changes the power profile for the
  other. This is the one genuine conflict between the two protocols, and it is a
  tuning question, not a blocker.
- **Bonding/pairing state** — per device, hence shared. `GB_ENABLE_BONDING`
  (`gb_ble.cpp:181`) already covers the link both apps use.
- **Single-connection assumptions in the current code** — `s_conn_handle` holding one
  value, and `s_assembler.reset()` on connect, are correct for one phone and wrong
  the moment a second *device* connects (a tablet running the Meshtastic app against
  a watch already bonded to a phone's Gadgetbridge). Multi-connection peripherals are
  well-precedented on ESP32-S3 (up to `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`), so this is
  a bounded fix to make in `gb_ble.cpp`, not a design risk — but it is a real code
  change, not free.

The genuinely new constraint this section previously missed is discovery, not
connection count: see §5.3 and §9 risk 8 for the advertising-payload problem.

## 5. Work items

### 5.1 Nanopb toolchain integration

Add **nanopb** (<https://github.com/nanopb/nanopb>) as a build dependency and
codegen step:

- Vendor or `lib_deps`-pull the nanopb runtime (`pb.h`, `pb_decode.c`/`.h`,
  `pb_encode.c`/`.h`, `pb_common.c`/`.h` — small, dependency-free C, so it should
  build cleanly for both the ESP32 Arduino envs and the native/SDL2 emulator envs
  the same way ArduinoJson already does for Gadgetbridge).
- Add a PlatformIO `extra_scripts` pre-build step (mirroring `support/sdl2_paths.py`
  /`support/sdl2_build_extra.py`'s existing pattern of Python helpers invoked from
  `platformio.ini`) that runs nanopb's `generator/nanopb_generator.py` (or the
  `protoc --nanopb_out=...` form) over the full schema pulled in (§5.2) and drops the
  generated `.pb.c`/`.pb.h` pairs somewhere under
  `src/new_interface/meshtastic_node/generated/` (git-ignored, regenerated at build
  time — matches how `.pio/libdeps` itself is not vendored, per this repo's existing
  "most libraries pulled via `lib_deps`, not vendored" convention noted in
  CLAUDE.md).
- Each `.proto` needs a matching `.options` file (nanopb's mechanism for bounding
  otherwise-unbounded `repeated`/`bytes`/`string` fields to fixed-size C arrays,
  since nanopb by design avoids heap allocation) — Meshtastic's own firmware repo
  already carries these `.options` files for its protos; start from those rather
  than deriving field-size limits from scratch.
- This is genuinely new toolchain surface for this repo — nothing here currently
  runs a codegen step at build time — so budget real time for getting the
  PlatformIO `extra_scripts` plumbing working before writing any protocol logic
  against it, and validate it first in the `emulator_watch_ultra` env where
  iteration is fast.

### 5.2 Protobuf message set: the full node-relevant schema

Unlike a pure client, a real node needs to round-trip the schema the Meshtastic app
uses to onboard and drive any node it connects to:

- `mesh.proto` — `ToRadio`, `FromRadio`, `MeshPacket`, `Data`, `MyNodeInfo`,
  `NodeInfo`, `User` (node's display name/short name); `Position`/`Telemetry` once
  those payload types are prioritized (the envelope handles them regardless — this
  is about when to build UI/logic for their *contents*, not the framing).
- `portnums.proto` — the full `PortNum` enum, so the node can recognize/route ports
  it doesn't render UI for, not just `TEXT_MESSAGE_APP`.
- `channel.proto` — `Channel`, **including the PSK field this time**. Unlike the
  pre-revision client-only plan (which could defer crypto to a paired node), the
  watch is now the node doing the en/decryption itself (§4.1 item 3), so it needs
  the real channel/key config, not just the channel list.
- `config.proto` / `module_config.proto` — `LoRaConfig` (region, modem_preset) and
  the other `Config` sections the Meshtastic app edits via the admin channel.
- `admin.proto` — `AdminMessage` (remote configuration). The Meshtastic app
  configures a freshly-added node primarily through admin messages over this same
  BLE link, not a separate mechanism, so this is required for the app to be able to
  onboard the watch the normal way — not an optional extra.

### 5.3 Second peripheral GATT service (not BLE-central)

`gb_ble.cpp` demonstrates the peripheral (GATT server) pattern this reuses directly:
add a second NimBLE service (UUID `6ba1b218-15a8-461f-9fa8-5dcae273eafd`) with its
three characteristics (ToRadio/FromRadio/FromNum, §3) to the same NimBLE server
Gadgetbridge's NUS/DIS/Battery services already run on. This is new *service*
plumbing, not a new BLE *role* — see §4.2 for why that distinction matters, and why
hosting both services for one phone does not mean hosting two connections.

**The hard part here is advertising, not the services themselves.** The Meshtastic
app discovers nodes by scanning for service UUID `6ba1b218-…`, so the watch must
advertise it — and there is no room. `startAdvertising()`
(`gb_ble.cpp:148-166`) already documents the budget it is working against: flags (3
bytes) plus an 18-character complete local name (20 bytes) consumes 23 of the
advertisement's 31, and a 128-bit service UUID needs 18, which is why the NUS UUID
was pushed into the scan response in the first place. A second 128-bit UUID needs
another 18 bytes, and 18 + 18 = 36 does not fit in the 31-byte scan response either.
Both UUIDs cannot be advertised the way this code advertises one. The options, none
free:

- **Shorten the advertised name** to buy the 18 bytes in the advertisement itself,
  putting one UUID in each PDU. `GB_ADVERTISED_NAME_PREFIX " %02X%02X"`
  (`gb_ble.cpp:141-146`) currently yields something like `T-Watch Ultra 4F2A`; the
  name must keep matching protocol doc §3's regex for Gadgetbridge to recognise it,
  and `T-Watch Ultra` alone (13 chars, 15 bytes) fits with the MAC suffix dropped —
  at the cost of the suffix's stated purpose, telling two watches apart in a scan
  list.
- **Alternate the two UUIDs** across advertising intervals, so each scanner sees the
  one it wants within a scan window. Costs discovery latency and is fiddly.
- **BLE 5 extended advertising**, which the ESP32-S3 controller and NimBLE both
  support. Android scanner support for extended advertising is version- and
  chipset-dependent, so this cannot be assumed to reach every phone.

Which of the three is right depends on whether the Meshtastic app will connect to an
already-bonded, non-advertising device without a fresh scan hit — untested here.
Verify this early (§6 step 2): it is cheap to test with the current firmware plus a
generic BLE scanner app, long before any protobuf work exists.

### 5.4 `meshtastic_node/` module shape (mirrors `gadgetbridge_ble/`)

- `mtc_protocol.*` — pure protobuf encode/decode around the nanopb-generated
  structs; no BLE/LVGL dependency, compiles for hardware and emulator.
- `mtc_node.*` (`MeshtasticNode`) — the state machine: radio bring-up (gated on
  §10's enable flag) → region/preset configuration → channel/key state → routing →
  ready. Owns the node database and identity, exposed via accessors plus a listener
  callback, same shape as `GbApp`/`GbStateChange`.
- `mtc_ble.cpp` — the second NimBLE peripheral service implementing §5.3. Not a
  central/scanning transport.
- `mtc_radio.*` — a thin adapter translating Meshtastic's `region`+`modem_preset`
  into the fields `hw_set_radio_params()` (`hal_interface.h`) already accepts. This
  reuses `hw_sx1262.cpp`'s existing driver rather than replacing it —
  `ui_radio.cpp`/`ui_msgchat.cpp` keep working exactly as they do today, coordinated
  with this module only through §10's shared radio-enable flag (whichever app is
  using the radio, the on/off setting gates all of them equally).
- Crypto (§4.1 item 3) and routing/dedupe (§4.1 item 4) as their own internal
  components of `mtc_node.*` or split out if they grow large enough to warrant it.
- Node database + identity/key persistence: a small NVS namespace of its own
  (mirroring `hal_interface.cpp`'s existing `Preferences`-backed pattern for
  `user_setting_params_t`, but **not** the same `"pager"` blob — a node identity key
  is not a user setting and should not share a struct/migration path with one).
- A native/emulator stand-in transport (mirroring `gb_link_stdio.cpp`) so the node
  state machine and UI are exercisable in `emulator_watch_ultra` without real BLE
  hardware or a real Meshtastic app — e.g. reading canned `ToRadio` byte sequences
  from stdin/a fixture file and writing `FromRadio` responses to stdout. This is
  explicitly called out because CLAUDE.md's project-wide convention (and `plan.md`'s
  own "make as much as possible runnable/testable in the simulator" instruction)
  both expect it.

### 5.5 `app_meshtastic.*` + `ui_meshtastic.cpp` — status UI only

Mirrors `app_gadgetbridge.h`/`.cpp` and a `ui_*.cpp` app: `app_meshtastic.cpp` owns
the one `MeshtasticNode::Listener` and fans it out; `ui_meshtastic.cpp` is a new
launchable `app_t` (registered in `ui_main.cpp` the same way `ui_radio.cpp` is)
showing node status (enabled/disabled, region set or not, BLE connection state), the
node list, and a minimal text-message view.

**Deliberately not a configuration UI.** Region selection, channel/PSK editing, and
full mesh management belong to the Meshtastic app on the phone — duplicating that
here would mean maintaining a second, watch-constrained copy of settings the
official app already does well, permanently out of sync with upstream Meshtastic UX
changes. The one exception is the LoRa on/off switch itself (§10.3), which is a
watch-local hardware control, not a Meshtastic protocol setting, and belongs in this
app's Settings screen regardless of what any phone app can reach.

### 5.6 Regulatory-safe defaults & region selection

No TX until the node has an explicit region set — mirrors Meshtastic's own
onboarding, where the app forces a region pick before the radio keys up. Practically:
ship with §10's LoRa-enabled flag defaulted to **off**, and additionally refuse to
leave standby once enabled until `LoRaConfig.region` (set via the admin channel from
the Meshtastic app, §5.2) is something other than `UNSET`. §10.4 covers how the
always-available on/off switch and this narrower, config-dependent gate relate.

## 6. Order of work

1. **§10 first** (persisted LoRa radio on/off setting) — small, independently
   useful on its own, and several later items (§5.6's gating, §5.4's radio bring-up)
   depend on the flag already existing.
2. **§5.3** advertising/discovery spike — the cheapest high-information test in this
   plan, and runnable against *today's* firmware with a generic BLE scanner app
   before a line of Meshtastic code exists. Establish (a) whether a scanner still
   finds and connects to the watch while Gadgetbridge holds it, (b) which of §5.3's
   three advertising strategies actually gets both UUIDs discovered, and (c) that two
   apps on one phone really do share one link as §4.2 states — the serial log shows
   one `onConnect` or two. Do this before §5.1: if (c) is wrong, or if no advertising
   strategy works, the coexistence design changes and everything downstream is built
   on a false premise.
3. **§11 duty-cycle decision**, with §11.2's light-sleep spike folded into step 2's
   hardware session (same character, same cost). §5.4's shape depends on the answer,
   and a bad answer here is a reason to revisit §7.1 before spending §5.1-§5.2's
   effort — so decide it while that is still cheap.
4. **§5.1** nanopb toolchain — prove a round-trip encode/decode of one trivial
   message compiles and runs correctly in `emulator_watch_ultra` before anything
   else. Highest-uncertainty *build* item, and it blocks everything downstream;
   only steps 2-3 above, which need none of it, come first.
5. **§5.2** pull in the full proto subset + `.options` files, generate, confirm the
   generated structs match what Meshtastic's own client API examples show on the
   wire (cross-check against `meshtastic-python`'s BLE interface output, or a real
   node's own byte stream, if possible).
6. **§5.4** `meshtastic_node/` module: radio bring-up (gated on §10) → config
   handshake → channel crypto → routing → node database, tested against the real
   Meshtastic phone app and, ideally, a real second Meshtastic node to confirm actual
   mesh interop (not just a watch↔phone round trip).
7. **§5.6** regulatory defaults, once region config (part of §5.2/§5.4) exists to
   enforce against.
8. **§5.5** on-watch status UI.
9. Update `.claude/twatch-ultra-ble-protocol.md` only if §10.3's phone-syncable
   on/off flag is added to the `settings` message — see the companion edit to
   `watch-settings-sync-protocol-plan.md`. Nothing else in this plan touches the
   Gadgetbridge contract.

## 7. Phone-side management: a separate app, not Gadgetbridge

Per the user's direction, "managing the connection from the phone" (the original ask
in `src/custom_interface/plan.md`) is satisfied by the official Meshtastic app (or
any standards-compliant Meshtastic BLE client) talking directly to the watch's new
GATT service from §5.3 — the same way it talks to any other Meshtastic node. **No
Gadgetbridge fork changes are needed for this feature.** The pre-revision plan's §7
("Phase 2: phone-managed connection" as new Gadgetbridge message types) is
withdrawn in full — that design existed only because the earlier plan assumed the
phone would manage the connection *through* Gadgetbridge; it doesn't.

### 7.1 "Can Meshtastic reach the watch *through* Gadgetbridge instead?" — asked 2026-08-17

Recorded here because it is the obvious question to ask of §4.2's coexistence story,
and the answer constrains any future revisit. Two directions, only one of which
exists:

- **Tunnelling Meshtastic frames over the Gadgetbridge NUS link** (a new `t` value
  carrying `ToRadio`/`FromRadio` bytes) is straightforward on the watch and a dead
  end on the phone. The Meshtastic Android app's transports are its own BLE service,
  TCP/IP, and USB serial; there is no hook to source its frames from another app. It
  would have to be forked alongside Gadgetbridge, leaving two forks maintained
  against an upstream protobuf schema — exactly what §2.1 rejects, doubled. Not
  viable, and not merely out of scope.
- **The reverse — Gadgetbridge as a client of the Meshtastic app** — does exist. The
  Meshtastic Android app exposes an AIDL service and broadcast intents for
  third-party apps (`com.geeksville.mesh.IMeshService`, `RECEIVED.*` /
  `MESH_CONNECTED` broadcasts; the integration path ATAK-style plugins use). The
  Gadgetbridge fork could subscribe to incoming mesh text and forward it to the watch
  as ordinary notification/chat messages over the existing NUS link, sending replies
  back out through the same service API. That needs **no** new watch-side BLE
  service, no nanopb, no AES-CTR, no routing, no region tables — days of work against
  this plan's weeks-to-months.

**This does not replace the plan.** It is a different feature: it makes the watch a
mesh *display and remote* for a mesh whose node lives elsewhere, which is precisely
what §4.0 rules out on explicit direction. It is written down so that if the priority
ever shifts from "the watch is the node" to "mesh messages on my wrist, cheaply,"
the cheap option is already scoped and its cost is known. The two are not mutually
exclusive either — a watch that *is* a node has no use for the AIDL bridge, but
nothing about §4 forecloses it later.

Verify the AIDL/intent surface against current upstream before relying on it; the
same drift caveat as §9 risk 3 applies, and it has not been checked against a real
install here.

One small, genuinely optional idea if wanted later, kept explicitly separate from
this plan: a cosmetic Gadgetbridge status mirror ("mesh node: on/off," surfaced
through the `lora_enabled` field §10.3 already adds to the general settings-sync
mechanism) — since that field is watch-local hardware state, not a Meshtastic
protocol concept, showing it in the general Gadgetbridge settings screen doesn't
reintroduce any of the reasons §2.1/§7 keep Meshtastic itself out of that protocol.
This is not required for Meshtastic to work and should not be scoped as part of this
plan; it would be its own small addition to `watch-settings-sync-protocol-plan.md`
if ever wanted.

## 8. Test matrix (§4-§5, once built)

| Case | Expected |
| --- | --- |
| LoRa disabled in Settings (§10), Meshtastic app tries to connect | BLE service is still discoverable (or not advertised at all — an implementation choice, §10.2), but the node refuses to leave standby / shows "LoRa is off" rather than silently failing |
| LoRa enabled, region unset | Node completes the BLE config handshake and is configurable, but will not transmit (§5.6) until a region is set from the app |
| Region set, single channel (Default, PSK `0x01`) | Node joins the mesh at LongFast defaults; a real Meshtastic node/app can see it, exchange `TEXT_MESSAGE_APP` packets |
| Region set, custom channel with random PSK | Node correctly encrypts/decrypts against that channel; a mismatched PSK on the far end produces silently-dropped packets, not a watch-side error (expected per §3) |
| Gadgetbridge fork + Meshtastic app, **same phone** | Both stay functional over the one shared link (§4.2); serial log shows a single `onConnect`. Neither app's MTU or connection-interval needs disturb the other enough to break it |
| Gadgetbridge fork + Meshtastic app, **two different devices** | Two real connections; watch serves both, or the single-`s_conn_handle` assumption in `gb_ble.cpp` is fixed to track them separately (§4.2) |
| Meshtastic app scans while Gadgetbridge is connected | Watch is discoverable on the Meshtastic service UUID under whichever §5.3 advertising strategy was chosen, and the Gadgetbridge name still matches protocol doc §3's regex |
| Screen off / watch idle, another node sends traffic needing a relay | Per whichever §11 option was chosen: C relays it; B relays it if it lands in a wake window; A does not relay at all. The point of the test is that the *chosen* behaviour is what actually happens — not that relaying always works |
| Wrist drops (screen off) mid-conversation with the Meshtastic app | BLE connection survives the idle light sleep, or §11.2's spike already established it does not and the design accounts for it |
| Node goes out of range of other mesh members | No crash; node database entries age out or are marked stale rather than accumulating indefinitely |
| Malformed/unexpected `ToRadio` frame from the phone app | Dropped without crashing the parser, mirroring Gadgetbridge protocol's "malformed JSON drops that line only" resilience posture |
| Send a text message from the watch | Arrives at a real Meshtastic client (phone app, second node) as a normal `TEXT_MESSAGE_APP` packet — the actual interop proof |
| Receive a text message from the mesh | Shows up in the Meshtastic app (primary) and on the watch's own status UI (§5.5), attributed to the sending node |
| Toggle LoRa off while the node is mid-mesh (relaying for others) | Radio goes to standby immediately (§10.2); watch drops off the mesh from other nodes' point of view, same as physically powering off a node (§10.4) — expected, not a bug |
| Battery drops to `LORA_BATTERY_SAVER_PERCENT` while `lora_enabled` is still true | Radio forced to standby immediately (§10.5); Settings switch still shows "on" with a "battery saver" note; `ui_radio.cpp`/`ui_msgchat.cpp` show "LoRa is off to save battery" |
| Battery recovers to `LORA_BATTERY_SAVER_REARM_PERCENT`, or charger is connected | Override clears; radio usable again next time a LoRa app is opened (no proactive re-arm mid-session) |
| `emulator_watch_ultra` with the stdin/fixture transport (§5.4) | Node state machine and UI are exercisable without hardware, per this repo's simulator-first convention |

## 9. Risks

1. **BLE coexistence — downgraded, and largely superseded by risk 8.** Two prior
   drafts each named a coexistence concern as the top risk: dual central+peripheral
   roles (withdrawn in §4.2 when the watch became the node), then two concurrent
   central connections (corrected 2026-08-17 — two apps on one phone share a single
   Android-multiplexed link, so there is no second connection in the case that
   matters; see §4.2). What survives is smaller and bounded: a shared connection
   interval both protocols must live with, and `gb_ble.cpp`'s single-`s_conn_handle`
   assumption, which needs fixing only to support a Meshtastic client on a *separate
   device*. Confirm the shared-link behaviour empirically in §6 step 2 rather than
   trusting this paragraph — it is a statement about Android's stack, not something
   verified against this watch.
2. **Nanopb `.options` field-size choices are a real design decision, not
   boilerplate.** Undersized `repeated`/`bytes` bounds silently truncate; oversized
   ones waste the watch's limited RAM. Start from Meshtastic firmware's own
   `.options` files rather than guessing.
3. **Protocol drift.** Meshtastic's protobuf schema and BLE UUIDs are
   independently versioned upstream; this plan's §3 facts are current as of
   2026-08-15 but should be re-verified against
   <https://github.com/meshtastic/protobufs> and
   <https://meshtastic.org/docs/development/device/client-api/> at implementation
   time, not trusted indefinitely from this document.
4. **RadioLib version drift.** §2.2's findings are against the vendored 7.1.2; a
   `lib_deps` bump could change what RadioLib does or doesn't expose without this
   plan being revisited — now more load-bearing than before, since the watch's own
   radio bring-up (§5.4's `mtc_radio.*`) depends on it directly rather than the
   (withdrawn) client option's ability to skip PHY work entirely.
5. **No reference node in-repo, but a well-known external reference exists.**
   Unlike Gadgetbridge (which has a fork of the actual Android app to test with, per
   CLAUDE.md), this plan has no equivalent "known-good other end" checked into this
   repo. Testing needs the real Meshtastic phone app plus, ideally, a second real
   Meshtastic node to confirm genuine mesh interop (not just a watch↔phone round
   trip); `meshtastic-python` (CLI, works against any node's client API) is useful
   for byte-level cross-checking during development. Budget for acquiring one before
   implementation starts on §5.4, not after.
6. **Node identity/key persistence is a real security surface now.** Because the
   watch is the node (not a client deferring identity to a paired device), its
   private key lives in the watch's own NVS (§5.4). Physical access to the watch is
   access to that identity, the same ceiling any physical Meshtastic node hardware
   already has — not a new weakness introduced by this plan, but worth stating
   explicitly since the pre-revision client-only design wouldn't have had this
   concern at all.
7. **Scope is now fixed at full mesh-node behavior; there is no smaller fallback
   milestone.** The pre-revision plan's "(a) first" gave an earlier, smaller
   shippable point. That option is withdrawn because it doesn't match what was
   asked for (§4.0), so §4.1's full list is the actual first milestone — plan
   schedule/expectations accordingly; §5.1-§5.4 is the section that has to land
   before anything is demoable against a real Meshtastic app.
8. **Both service UUIDs do not fit in the advertising payload — now the top
   coexistence risk**, replacing risk 1. Detailed in §5.3: the advertisement and scan
   response between them cannot carry an 18-byte 128-bit UUID twice alongside a
   Gadgetbridge-matching device name, and the Meshtastic app finds nodes by scanning
   for its UUID. Every mitigation costs something (a shortened name loses the
   two-watches-apart MAC suffix; alternating UUIDs costs discovery latency; BLE 5
   extended advertising is not universally scannable on Android). Unlike the
   connection-count concern it replaces, this one is certain — it follows from the
   31-byte PDU limit and the byte budget `gb_ble.cpp:152-154` already documents — so
   the spike (§6 step 2) is choosing a mitigation, not deciding whether a problem
   exists.

## 10. New: persisted LoRa radio on/off setting

### 10.1 What it controls

A single, persisted "LoRa radio enabled" flag that gates every consumer of the
SX1262 radio in this app — the existing raw-PHY test app (`ui_radio.cpp`), the
existing broadcast chat (`ui_msgchat.cpp`), and the new Meshtastic node module
(§5.4) alike. When off, the radio is put to (and kept in) RadioLib
`standby()`/sleep and none of those three call sites are allowed to key it up. This
is the "antenna off" the user asked for: a hard, watch-local override, independent
of which LoRa-using app is open, and a coarser control than §5.6's node-specific
region gating (region-unset blocks the *Meshtastic node* from transmitting; this
flag blocks the *radio* entirely, for any of the three consumers — see §10.4).

### 10.2 Where it lives

Follows the existing `user_setting_params_t` pattern (`hal_interface.h`/`.cpp`,
NVS-backed under the `"pager"` namespace — see
`watch-settings-sync-protocol-plan.md` §4 for the precedent) rather than
`RTC_DATA_ATTR` — silently losing "the user explicitly turned the radio off" on a
power cycle is the wrong default for what is partly a regulatory/privacy control,
not just a UI convenience.

- New field: `bool lora_enabled` in `user_setting_params_t`, defaulting to
  **false** (§5.6's "fail closed" reasoning applies here too — ship with the radio
  off until the user turns it on, not just until a region is separately picked).
- New accessor pair in `hal_interface.h`/`.cpp`: `hw_get_lora_enabled()` /
  `hw_set_lora_enabled(bool)`, following the shape of the existing settings
  getters/setters. `hw_set_lora_enabled(false)` should force an immediate
  `radio.standby()` (or a deeper RadioLib sleep mode, if the SX1262 back end
  already in use exposes one) rather than waiting for the next radio-owning app to
  notice — "off" should be immediate and externally observable (e.g. on an SDR),
  not eventually-consistent.
- `ui_radio.cpp`, `ui_msgchat.cpp`, and `meshtastic_node/mtc_node.*` (§5.4) each
  check `hw_get_lora_enabled()` before calling into `hw_set_radio_params()` or
  starting TX/RX, and should show a clear "LoRa is off — enable it in Settings"
  state rather than silently doing nothing.

### 10.3 Settings UI + phone sync

- On-watch: a `create_switch()` toggle (existing helper, `ui_tools.cpp:341`) in the
  settings screen, alongside the app's other hardware toggles.
- Phone-syncable: add `lora_enabled` as a sixth field to
  `watch-settings-sync-protocol-plan.md`'s `settings` message pair (its §3.1/§3.2),
  alongside `notif_timeout_ms`/`notif_vibrate`/`pinned_mask`/`clock_mode`/
  `low_batt_pct` — same partial-update-on-the-way-in, full-state-echo-on-the-way-back
  shape, same per-field clamping/validation style. This travels over the
  *Gadgetbridge* connection (general watch settings), a separate concern from
  §5.3's new Meshtastic GATT service — the phone doesn't need the Meshtastic app
  open to flip this switch, and flipping it off should visibly disconnect anything
  currently attached to the Meshtastic service (the app should show "disconnected,"
  not hang). See the companion edit made to `watch-settings-sync-protocol-plan.md`
  for the exact field addition (its §3/§4/§5/§7).

### 10.4 Interaction with §5.6's region-gating and with mesh routing

Two independent gates end up in front of TX, and both are needed:

- **§10's `lora_enabled`** — a blunt, always-available, user-facing kill switch
  covering every LoRa consumer in the app (raw radio test, broadcast chat,
  Meshtastic node alike). Off means off, full stop, regardless of what any of those
  three subsystems think their own state is.
- **§5.6's region-unset gate** — narrower and Meshtastic-specific: even with
  `lora_enabled = true`, the *node* still won't transmit until it has a real region
  configured (mirrors Meshtastic's own onboarding). This only applies to the node
  module; it has no bearing on `ui_radio.cpp`/`ui_msgchat.cpp`, which have no
  concept of "region."

Turning the radio off (§10) on a node that's currently relaying traffic for other
mesh members drops it from the mesh immediately and silently from their point of
view — the same as physically powering off any Meshtastic node. This is expected,
user-directed behavior, not a bug to guard against; it should not be "fixed" later
by e.g. deferring the toggle until routing looks idle.

### 10.5 Battery-saver: an automatic third gate, layered on top of §10's manual one

**Implemented**, alongside §10.1-§10.4. §10's `lora_enabled` is a *manual* switch —
it does nothing on its own if the user leaves it on and the watch's battery runs
down while the radio (or a future always-on node) keeps drawing power. A
battery-powered watch acting as a Meshtastic bridge is a materially different power
budget than the mains/solar/large-pack nodes Meshtastic is usually deployed on, so
this needs its own, automatic gate:

- `hal_interface.h`/`.cpp`: `hw_get_lora_battery_saver_active()` (read-only status)
  and `hw_lora_radio_allowed()` — the actual gate every radio-owning call site must
  check, equal to `hw_get_lora_enabled() && !hw_get_lora_battery_saver_active()`.
  Critically, `hw_get_lora_enabled()` itself is **unchanged** by battery-saver — it
  keeps meaning "the user's own persisted preference," which is what the Settings
  switch (§10.3) and the phone-synced `lora_enabled` field both read/write. The
  override is a separate, in-memory-only bit (`hw_lora_battery_saver_poll()`,
  called from `ui_main.cpp`'s existing `hw_device_poll()` battery timer, same 5 s
  cadence already used for the hard-shutdown check), so it can never leak into NVS
  or get echoed to the phone as if the user had turned the radio off.
- Thresholds (`app_config.h`): `LORA_BATTERY_SAVER_PERCENT` (10%) engages the
  override and forces the radio to standby immediately, the same way
  `hw_set_lora_enabled(false)` does; `LORA_BATTERY_SAVER_REARM_PERCENT` (15%) clears
  it, mirroring `LOW_BATTERY_WARNING_PERCENT`/`_REARM_PERCENT`'s existing hysteresis
  pattern so the radio doesn't flap on/off right at the threshold. Charging clears
  the override unconditionally — deliberately chosen well below
  `LOW_BATTERY_WARNING_PERCENT` (20%, a warning only) and well above the hard
  3300 mV shutdown floor in `hw_device_poll()`: the radio gives up its share of the
  battery budget before the watch warns the user, and long before it shuts down.
- `ui_radio.cpp`/`ui_msgchat.cpp` gate on `hw_lora_radio_allowed()` (not
  `hw_get_lora_enabled()` alone) and show "LoRa is off to save battery" instead of
  the generic "enable it in Settings" message when the override, not the user's own
  preference, is what's blocking them. `ui_sys.cpp`'s Settings switch still reflects
  the user's own preference (it can legitimately show "on" while the radio is
  actually off) with a status note explaining the discrepancy.

**Not implemented, and out of scope until the full node exists (§4.1):** Meshtastic's
own `power.proto` / `Config.PowerConfig` — `is_power_saving`, `ls_secs` (light-sleep
interval between mesh checks), `min_wake_secs`, `sds_secs` (super-deep-sleep),
`on_battery_shutdown_after_secs`, `adc_multiplier_override`. This is the *node's*
own configurable duty-cycling (skip listening most of the time, wake briefly to
check the mesh), set by the Meshtastic app over the admin channel like any other
Meshtastic node's power config, and it's a real, separate mechanism from §10.5's
blunt watch-level override:

- §10.5 is a coarse, watch-wide "radio budget is gone, stop entirely" cutoff that
  applies today, before any node code exists, to the raw-PHY apps too.
  `power.proto` is Meshtastic-specific, fine-grained duty-cycling that only makes
  sense once `meshtastic_node/` (§5.4) exists to have something to duty-cycle.
- Add `power.proto` to §5.2's schema list when that work starts, and have
  `mtc_node.*` honor it the way real Meshtastic firmware does (reduced-duty-cycle
  RX windows, not full standby) — a node that's fully asleep per §10.5 can't relay
  for the mesh at all, whereas a node in Meshtastic's own power-saving mode still
  participates, just less promptly. Both mechanisms should coexist: §10.5 as the
  hard floor, `power.proto`'s setting as the node's own finer-grained behavior above
  that floor.
- This also interacts with the watch's separate, pre-existing CPU/display
  power-save state machine (`ui_main.cpp`'s `ui_poll_timer_callback()`:
  240→80 MHz idle downclock, then light sleep via `instance.lightSleep()`, gated by
  `set_low_power_mode_flag()`/`DEVICE_CAN_SLEEP`) — that machine only runs while the
  home tile is in front and currently has no notion of "a background service needs
  to keep running regardless of what's on screen." An always-on mesh node is
  exactly such a service: unlike today's apps (which hold `DEVICE_CAN_SLEEP`
  cleared only while their own screen is open), the node needs to keep receiving
  even when the user has navigated away entirely, which the current per-app flag
  does not model. **Promoted out of this footnote into §11**, which costs the options
  out and makes it a decision to take before §5.4 rather than during it.

## 11. Duty cycle: how much of the time is the watch actually a mesh member?

Added 2026-08-17, promoted from §10.5's closing paragraph. This is the decision where
"smartwatch" and "mesh node/antenna" genuinely pull against each other, and it is not
protocol work — none of §5's items answer it. It shapes what `mtc_node.*` (§5.4) is,
so take it before building that, not during.

### 11.1 The conflict, concretely

A mesh node is useful to *other people* in proportion to how much of the time it is
listening. A watch is useful to its wearer in proportion to how little of the time its
SoC is awake. In this checkout the second goal currently wins unconditionally:

- The idle path light-sleeps the SoC (`hw_low_power_loop()`,
  `hal_interface.cpp:2248`, calling `instance.lightSleep()`), driven by
  `ui_main.cpp`'s poll timer with a 240→80 MHz downclock before it.
- Whether the SoC may sleep at all is a single global flag,
  `set_low_power_mode_flag()` / `DEVICE_CAN_SLEEP` (`ui_main.cpp:96,132`), which apps
  clear only while *their own screen is in front* (`ui_nfc.cpp:68`, `ui_msg.cpp:48`,
  `ui_boot_button.cpp:108`). There is no representation of "a background service needs
  the SoC awake regardless of what is on screen" — which is exactly what a mesh node
  is.
- The radio's DIO1 handler (`hw_sx1262.cpp:52`) is a normal attached interrupt that
  sets an event-group bit. A plain GPIO interrupt does **not** wake an ESP32 out of
  light sleep unless that pin is separately registered as a light-sleep wake source.

### 11.2 Prerequisite spike (do this first — it may remove options)

Two unverified facts gate everything below, both testable against today's firmware
with no Meshtastic code in front of them:

1. **Can DIO1 wake the SoC from `instance.lightSleep()`?** If not, options B and C
   below require either changing how LilyGoLib's light sleep is entered, or keeping
   the SoC out of light sleep entirely whenever the node is enabled — which collapses
   B into C on power terms.
2. **Does an active BLE connection survive `instance.lightSleep()`?** The comment at
   `hal_interface.cpp:2240-2247` claims timers, WiFi and the RTC keep running but says
   nothing about the BLE controller. If a connected Gadgetbridge or Meshtastic app
   drops on every idle sleep, that constrains the answer independently of LoRa.

Run this alongside §6 step 2's BLE spike — same "cheap, hardware-only, no toolchain"
character.

### 11.3 The options

| | A. On-demand | B. Duty-cycled | C. Always-on RX |
| --- | --- | --- | --- |
| **RX window** | Only while the Meshtastic app is connected or the watch's mesh screen is open | Periodic wake windows, per Meshtastic's own `power.proto` (`ls_secs`, `min_wake_secs`) | Continuous whenever `hw_lora_radio_allowed()` (§10.5) is true |
| **Is it an antenna?** | No — relays nothing, invisible between sessions | Partly — relays what lands in a wake window; other nodes see a node with latency | Yes — a real relay |
| **Mesh peers see** | A node that is almost always absent | A slow but present node | A normal node |
| **Battery cost** | Effectively nil beyond today | Proportional to duty cycle; the tunable knob | Highest: continuous SX1262 RX is a datasheet-order few-to-low-tens of mA, against a watch cell — **measure before committing**, do not trust this row |
| **Code cost** | Lowest — reuses the existing per-app `DEVICE_CAN_SLEEP` model unchanged | Highest — needs a background-service concept in `ui_main.cpp`'s sleep machine, DIO1 wake (§11.2), and `power.proto` in §5.2's schema | Middle — needs the same background-service concept, but no duty-cycle state machine; node enabled ⇒ never set `DEVICE_CAN_SLEEP` |
| **Honest summary** | A Meshtastic *client* that happens to own the radio — see §4.1a for why that is nearly the same product as not being a node at all | What real battery-powered Meshtastic nodes do | A mesh node that is also a watch, with the battery life that implies |

### 11.4 Recommendation

**Build C, ship B.** They share the one structural change — teaching `ui_main.cpp`'s
sleep machine that a background service can hold the SoC awake independently of the
front screen — and C is that change with no state machine on top, which makes it the
right first target: it proves the node genuinely relays, on hardware, before any
power tuning muddies the picture of whether the mesh behaviour itself is correct.
Then add `power.proto`'s duty-cycling (B) as the shipping default once relaying is
proven, since B is Meshtastic's own answer to exactly this problem and is configured
from the phone app like any other node's power config, requiring no watch UI.

A is not recommended for the stated goal: per §4.1a, a node that only listens while
you are looking at it delivers presence and independence but not the antenna, which
is most of what the node role was chosen for — while still costing every item in
§4.1. If A turns out to be the only viable option after §11.2, that is a signal to
revisit §7.1's much cheaper phone-side route rather than to build §4.1 for an
on-demand-only node.

Whichever is chosen, §10.5's battery-saver override sits underneath it unchanged: at
`LORA_BATTERY_SAVER_PERCENT` the radio stops regardless, and the watch drops off the
mesh exactly as §10.4 describes.
