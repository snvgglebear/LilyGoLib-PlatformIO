# Plan: real Meshtastic protocol interop for the watch

**Target:** the `new_interface` branch of this repo (`/workspaces/LilyGoLib-PlatformIO`),
`src_dir = src/new_interface`. Files that would be added or changed:

- New: `src/new_interface/meshtastic_client/` — a self-contained module (client state
  machine + BLE-central transport + protobuf codec), analogous in shape to
  `src/new_interface/gadgetbridge_ble/`.
- New: `src/new_interface/app_meshtastic.h` / `.cpp` — the UI-facing seam, mirroring
  `src/new_interface/app_gadgetbridge.h` / `.cpp`.
- New: `src/new_interface/ui_meshtastic.cpp` — the launchable app screen(s), registered
  in `ui_main.cpp` the same way `ui_radio.cpp`/`ui_walkie.cpp` are.
- Changed: `platformio.ini` — add a nanopb code-generation step and its library
  dependency to `env_arduino`/`env_emulator`.
- Changed (phone side, out of scope for this repo): a new "connect to Meshtastic
  node" flow would live in the Gadgetbridge fork referenced by CLAUDE.md, **not** in
  the `TWatchUltraDeviceSupport.java` BLE class — see §6.

No existing file's behaviour changes as a prerequisite for this plan. `hal_interface.h`'s
raw-radio functions (`hw_set_radio_params()` etc.) and `ui_msgchat.cpp`'s broadcast
chat are left exactly as they are; §2 explains why this plan does not touch them.

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
  - This is the model this plan follows for `meshtastic_client/`: pure codec, a
    state-machine class, a swappable transport, and an `app_meshtastic.*` fan-out
    seam — structurally parallel to Gadgetbridge, but a **separate module with its
    own listener**, not a hook into `GbProtocolHandler`. §2.1 explains why the two
    must not merge.
- **`src/custom_interface/plan.md` line 3** is the origin of this task: "lora/meshtastic
  functionality (primarily managing the connection from the phone, but also
  supporting a bluetooth keyboard.)" — phone-managed connection lifecycle is the
  explicit ask, not just packet exchange.

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
  (Android app, iOS app, Web, CLI, this watch). Re-encoding `ToRadio`/`FromRadio`
  protobuf frames as JSON fields inside `{"t":"notify",...}`-style messages would
  mean maintaining a translation layer against an external, independently-versioned
  schema forever, and would make this watch unable to talk to a real Meshtastic node
  without a Gadgetbridge fork in the loop translating for it — defeating "real
  protocol interop."
- **The transport identity is different and load-bearing.** Meshtastic's own BLE
  service UUID (`6ba1b218-15a8-461f-9fa8-5dcae273eafd`, confirmed by the current
  `meshtastic/firmware` BLE stack and documented at
  <https://meshtastic.org/docs/development/device/client-api/>) is what every
  Meshtastic-aware client (including a phone running the Meshtastic Android/iOS app,
  or a standalone puck node) scans for. If the watch is acting as a Meshtastic
  *client* (§4, option (a)), it needs to be the GATT **central**, connecting *to* a
  separate node's Meshtastic service — the opposite role from Gadgetbridge, where the
  watch is the GATT **server** the phone connects to (protocol doc §1: "The watch is
  the GATT server; the phone connects as client"). These are two independent BLE
  roles running concurrently on the same radio, not one relationship carrying two
  payload types.
- **Different consumer, different lifecycle.** Gadgetbridge messages are consumed by
  one phone app the watch is bonded to. Meshtastic packets are consumed by (or
  routed through) a mesh of possibly-unfamiliar nodes with their own identities,
  channel encryption, and hop-based routing — concepts that have no Gadgetbridge
  equivalent and would corrupt that protocol's simplicity if forced in.

What *should* cross into Gadgetbridge JSON, if anything: a thin status/control
surface for the *phone-management UX* the user asked for — e.g. "is the watch
connected to a Meshtastic node," "here are the last N mesh messages," "send this
text to the mesh" — as new, clearly-scoped message types, analogous to how
`android-sms-notifications-plan.md` §3 adds fields to `notify` rather than
reinventing SMS transport. That is a *phase 3* idea (§7) once the watch↔node
Meshtastic link itself works, not a substitute for it.

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
source (`RadioInterface.cpp`/`RegionInfo` tables) as its own work item (§5.3).

**Conclusion: RadioLib is necessary infrastructure (it is confirmed literally the
same library Meshtastic's firmware itself calls into) but supplies zero Meshtastic
protocol compatibility on its own.** Everything above the SPI/register layer —
frequency-plan tables, modem presets, packet structure, encryption, routing — is new
code regardless of which of the options in §4 gets picked.

## 3. What "real Meshtastic protocol interop" is, precisely

Verified against Meshtastic's public docs and protobuf repo
(<https://meshtastic.org/docs/development/device/client-api/>,
<https://github.com/meshtastic/protobufs>) as of 2026-08-15:

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
  (handshake done), `log_record`, and more.
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
  drops rather than an error the watch can detect on its own.
- **Routing.** Managed flood routing: a node retransmits a packet it hears if
  `hop_limit > 0` and it hasn't seen that `(from, id)` pair recently, decrementing
  `hop_limit` each hop. A full mesh-node implementation needs a dedupe cache and the
  hop/rebroadcast logic; a pure client (talking to one node over BLE) does not — the
  node it's paired to does all of that and only ever hands the client packets
  addressed to it (or broadcasts) plus its own node database.
- **Regional frequency plans + modem presets.** `config.proto`'s `LoRaConfig`
  encodes a `region` enum (US, EU_868, EU_433, ANZ, CN, JP, …) and a
  `modem_preset` enum (`LONG_FAST`, `LONG_SLOW`, `LONG_MODERATE`, `MEDIUM_SLOW`,
  `MEDIUM_FAST`, `SHORT_SLOW`, `SHORT_FAST`, `SHORT_TURBO`, or a fully custom
  bandwidth/SF/CR); the firmware derives the actual center frequency from the
  region's band edge plus a channel-number-and-slot calculation, and the actual
  bandwidth/SF/CR from the preset. A node/mesh-node implementation must reproduce
  both tables exactly to interoperate; a BLE client (option (a) below) does not
  need either — it never touches the LoRa radio itself.
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

## 4. Client vs. mesh node: the scope decision

### Option (a) — BLE client to a separate Meshtastic node

The watch pairs, as GATT **central**, to an existing physical Meshtastic node (a
LilyGo/Heltec/RAK puck, or any device running Meshtastic firmware) and speaks the
ToRadio/FromRadio/FromNum API described in §3. The watch's own LoRa radio
(`hw_sx1262.cpp` et al.) is **not used for Meshtastic at all** under this option —
it stays free for `ui_radio.cpp`/`ui_msgchat.cpp`'s existing raw-PHY use, or could
even be a different physical module than the paired node's.

- Scope: implement the `ToRadio`/`FromRadio` protobuf messages actually needed for
  a *minimal* client (config handshake, receiving `NodeInfo`/`MyNodeInfo`, sending
  and receiving `TEXT_MESSAGE_APP` packets on one channel) — a small, well-bounded
  slice of `mesh.proto`, not the whole schema.
  No routing, no encryption implementation (the paired node does both), no
  frequency-plan tables.
  Nanopb toolchain integration is still required (§5.1) — the wire format is
  protobuf either way — but the *message set* compiled in is small.
- Requires: the user (or anyone testing this) to own or borrow a second,
  independent Meshtastic-capable device to pair with. This is a real constraint —
  it is not a "the watch is now a mesh node out of the box" feature — but it is
  what "managing the connection from the phone" (the user's own framing in
  `plan.md`) most directly describes: the phone isn't managing a LoRa mesh
  connection, it's managing *the watch's BLE connection to a node*, the same
  relationship shape Gadgetbridge already has with the phone.
- Estimated effort: **on the order of 1-2 weeks** for a functioning minimal client
  (config handshake + text messaging + node list), most of it in the nanopb
  toolchain setup (first-time cost) and the BLE-central connection state machine
  (NimBLE-Arduino supports central mode, but this repo currently only uses it as a
  peripheral in `gb_ble.cpp` — connecting *out* to another device, scanning,
  bonding, and handling a second concurrent BLE role alongside Gadgetbridge's own
  peripheral role, is new ground here and the main integration risk).

### Option (b) — the watch itself becomes a Meshtastic mesh node

The watch's onboard LoRa radio runs full Meshtastic-compatible mesh firmware: real
packet TX/RX at the correct region/preset-derived PHY settings, channel AES
encryption/decryption, flood routing with hop-limit and dedupe, and a live node
database built from overheard `NodeInfo` broadcasts. No second device is needed —
the watch *is* a full participant other Meshtastic nodes and apps can see and talk
to directly over LoRa.

Concrete scope, each a substantial task on its own:

1. Nanopb toolchain integration (§5.1) — same as option (a), but now compiling in
   the *entire* relevant schema (`mesh.proto`, `channel.proto`, `config.proto`,
   `portnums.proto`, `admin.proto`), not a minimal slice.
2. Regional frequency-plan + modem-preset tables (§5.3), transcribed from
   Meshtastic firmware source and kept in sync as upstream adds regions/presets.
3. AES-CTR channel encryption/decryption per packet, with correct nonce
   construction matching Meshtastic's own (`packet_id` + `from` node ID) — a
   mismatch here doesn't error, it silently produces mesh traffic no other node
   can read, which is a difficult-to-debug failure mode.
4. Flood-routing implementation: hop-limit decrement, a seen-packet dedupe cache
   sized against real mesh traffic volumes, rebroadcast timing/jitter to avoid
   collision storms — Meshtastic's own firmware has years of tuning here that a
   from-scratch implementation would not start with.
5. A live node database (`NodeInfo` ingestion, position/telemetry handling if
   supported) and the node-identity/key-management story (Meshtastic's own nodes
   generate a persistent public/private identity; a from-scratch implementation
   needs to decide whether to reproduce that or omit it, with device-list-UI and
   security implications either way).
6. Regulatory correctness: transmitting on the *wrong* frequency-plan-derived
   channel, or with the wrong duty cycle/power limits for the selected region, is
   the user's legal responsibility once the firmware makes it easy to get wrong —
   this needs explicit region selection UI and defaults that fail closed (no TX
   until a region is chosen), mirroring how Meshtastic's own onboarding forces a
   region pick before the radio keys up.

Estimated effort: **genuinely comparable to porting a meaningful slice of the
Meshtastic firmware itself — realistically several weeks to a few months** of
focused work to reach parity with a real Meshtastic node's basic behavior, plus
ongoing maintenance to track upstream protocol/preset changes. This is not "a
feature you add to a watch app," it's closer to embedding a second firmware
project.

### Recommendation: start with (a), keep (b) as an explicitly separate, later phase

Do **(a)** first. It is a real, working, useful, honestly-scoped deliverable —
the watch talks genuine Meshtastic protocol to genuine Meshtastic hardware — sized
appropriately for "plan the next feature," not "start a second firmware project."
It also directly matches the user's own framing in `plan.md` ("managing the
*connection*," singular, from the phone) rather than the much larger claim of
"the watch joins a self-managed mesh."

Do **not** promise (b) as part of the same effort. If full mesh-node capability
is wanted later, it is worth scoping as its own dedicated plan document once (a)
has shipped and there's a working nanopb/protobuf toolchain and BLE-central
plumbing to build on — most of §5's toolchain work (item 1) is shared between the
two options, so (a) is not wasted effort if (b) is pursued afterward.

(c) (a Gadgetbridge-protocol status/control surface layered on top) is worth
doing, but as a thin addition *after* (a) works end-to-end — see §7. It is not an
alternative to (a)/(b), it's a phone-UX layer on top of whichever of them exists.

## 5. Work items (all for option (a), the recommended first milestone)

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
  `protoc --nanopb_out=...` form) over the subset of `.proto` files pulled in
  (§5.2) and drops the generated `.pb.c`/`.pb.h` pairs somewhere under
  `src/new_interface/meshtastic_client/generated/` (git-ignored, regenerated at
  build time — matches how `.pio/libdeps` itself is not vendored, per this repo's
  existing "most libraries pulled via `lib_deps`, not vendored" convention noted in
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

### 5.2 Minimal protobuf message set for a client

Pull in only the `.proto` files (and only the messages within them) a minimal
client actually round-trips:

- `mesh.proto` — `ToRadio`, `FromRadio`, `MeshPacket`, `Data`, `MyNodeInfo`,
  `NodeInfo`, `User` (node's display name/short name — needed to show anything
  human-readable about mesh peers).
- `portnums.proto` — at minimum the `PortNum` enum, to recognize
  `TEXT_MESSAGE_APP` vs. everything else (everything else can be received and
  ignored/logged in this minimal client).
- `channel.proto` — `Channel`, enough to read the config handshake's channel list
  (needed to know which channel index to send on) — **not** the PSK/crypto path,
  since encryption for TX/RX in option (a) never has to be reasoned about by this
  code — the paired node does en/decryption before/after the BLE hop entirely.
- Explicitly deferred: `admin.proto` (remote node configuration — not needed to
  just talk on the mesh), `config.proto`/`module_config.proto` (device settings —
  only needed if this client will ever *configure* the paired node, which is out
  of scope for a first milestone), telemetry/position portnums (defer until text
  messaging works).

### 5.3 BLE-central transport (new ground for this repo)

`gb_ble.cpp` only demonstrates the peripheral (GATT server) role. This work item
is: scan for the Meshtastic service UUID, connect as central, discover
ToRadio/FromRadio/FromNum, subscribe to FromNum's notifications, and drive the
write-then-drain-on-notify request/response cycle the client API expects. Check
early whether NimBLE-Arduino (already a `lib_deps` entry per CLAUDE.md) can run
central and peripheral roles concurrently on this chip/stack without conflict,
since Gadgetbridge's own peripheral role (§2.1) needs to keep working
simultaneously — this is a real open risk, flagged again in §6.

### 5.4 `meshtastic_client/` module shape (mirrors `gadgetbridge_ble/`)

- `mtc_protocol.*` — pure protobuf encode/decode around the nanopb-generated
  structs; no BLE/LVGL dependency, compiles for hardware and emulator.
- `mtc_client.*` (`MeshtasticClient`) — the state machine: connection lifecycle
  (disconnected → scanning → connecting → config handshake →
  ready), node database, message send/receive, exposed via accessors plus a
  listener callback, same shape as `GbApp`/`GbStateChange`.
- `mtc_ble.cpp` — the NimBLE-central transport implementing §5.3.
- A native/emulator stand-in transport (mirroring `gb_link_stdio.cpp`) so the
  client state machine and UI are exercisable in `emulator_watch_ultra` without
  real BLE hardware or a real Meshtastic node — e.g. reading canned
  `FromRadio` byte sequences from stdin/a fixture file. This is explicitly called
  out because CLAUDE.md's project-wide convention (and `plan.md`'s own "make as
  much as possible runnable/testable in the simulator" instruction) both expect it.

### 5.5 `app_meshtastic.*` + `ui_meshtastic.cpp`

Mirrors `app_gadgetbridge.h`/`.cpp` and a `ui_*.cpp` app: `app_meshtastic.cpp` owns
the one `MeshtasticClient::Listener` and fans it out; `ui_meshtastic.cpp` is a new
launchable `app_t` (registered in `ui_main.cpp` the same way `ui_radio.cpp` is)
showing connection status, the node list, and a minimal text-message view. "Managing
the connection from the phone" (§0) is a *later* addition on top of this — see §7 —
this item is the on-watch UI only.

## 6. Order of work

1. **§5.1** nanopb toolchain — prove a round-trip encode/decode of one trivial
   message compiles and runs correctly in `emulator_watch_ultra` before anything
   else. This is the highest-uncertainty item and blocks everything downstream.
2. **§5.3** BLE-central proof of concept — confirm NimBLE-Arduino can scan +
   connect as central on this hardware, and confirm it can coexist with
   Gadgetbridge's peripheral role if both need to run at once (§6/risks). If they
   cannot coexist, that changes the whole design (see risk below) and should be
   discovered before §5.2/§5.4 are built out.
3. **§5.2** pull in the minimal proto subset + `.options` files, generate, confirm
   the generated structs match what Meshtastic's own client API examples show on
   the wire (cross-check against `meshtastic-python`'s BLE interface output if
   possible, since it's a working reference implementation of the same client API).
4. **§5.4** `meshtastic_client/` module: connection state machine + config
   handshake + text-message send/receive, tested against a real Meshtastic node.
5. **§5.5** on-watch UI.
6. Phone-management layer (§7) as a follow-up phase, once 1-5 are solid.
7. Update `.claude/twatch-ultra-ble-protocol.md` only if/when §7 adds Gadgetbridge
   message types — nothing before that touches the Gadgetbridge contract.

## 7. Phase 2 (not in this milestone): phone-managed connection

Once the watch↔node client (option (a)) works stand-alone, "managing the connection
from the phone" — the user's explicit ask — is naturally a **new, small set of
Gadgetbridge message types** (§2.1's exception), since the phone already has a
channel to the watch and that's the right place for "which node to connect to,"
"connection status," and "send this text to the mesh" to live. Sketch (not a
commitment — write this up properly as its own addition, following
`android-sms-notifications-plan.md`'s §3 format, once phase 1 is real):

- Watch → phone: `meshstatus` (connected node id/name, link state).
- Phone → watch: `meshconnect` (target node's BLE address, if the phone did the
  scanning) or a simple `meshtoggle` if the watch does its own scanning and the
  phone only starts/stops it.
- Watch → phone: `meshmsg` (a received mesh text message, forwarded for display
  in Gadgetbridge itself as an alternate transport — genuinely new territory for
  Gadgetbridge, flag as needing upstream Android-side design too, per
  `plan.md`'s "any parts of this implementation that need to be done in
  gadgetbridge app should have an implementation plan written up").

This phase needs its own plan document when it's time; not fleshed out further
here so this document stays focused on the LoRa/Meshtastic side.

## 8. Test matrix (option (a), once built)

| Case | Expected |
| --- | --- |
| Scan with no Meshtastic node in range | UI shows "not found," no crash, retries or times out cleanly |
| Scan finds a node, connect | Config handshake completes (`config_complete_id` received), node's `MyNodeInfo`/own `User` shown |
| Node has multiple channels configured | Client reads the channel list; UI lets the user pick which channel to send on (or defaults to primary/index 0) |
| Send a text message | Arrives at another real Meshtastic client (phone app, second node) as a normal `TEXT_MESSAGE_APP` packet — this is the actual interop proof, not just a watch-side round trip |
| Receive a text message from the mesh | Shows up on the watch attributed to the sending node's name |
| Node goes out of BLE range mid-session | Client detects disconnect, UI reflects it, reconnect is possible without a firmware restart |
| Paired node reboots | Client re-handshakes cleanly on reconnect, does not accumulate stale node-database entries indefinitely |
| Malformed/unexpected `FromRadio` frame | Dropped without crashing the parser, mirroring Gadgetbridge protocol's "malformed JSON drops that line only" resilience posture |
| Two BLE roles at once (Meshtastic central + Gadgetbridge peripheral) | Both stay functional concurrently, or the plan is revised — see §9 risk 1 |
| `emulator_watch_ultra` with the stdin/fixture transport (§5.4) | Client state machine and UI are exercisable without hardware, per this repo's simulator-first convention |

## 9. Risks

1. **NimBLE central + peripheral concurrency is unproven in this codebase.**
   Everything BLE-related here today (`gb_ble.cpp`) is peripheral-only. If
   NimBLE-Arduino cannot run both roles at once on this chip/stack (or can, but
   with real throughput/stability costs), the watch cannot be simultaneously
   paired to the phone (Gadgetbridge) and to a Meshtastic node (this plan) without
   further design — e.g. time-slicing the radio, or accepting that Meshtastic
   mode and Gadgetbridge mode are mutually exclusive on this firmware. Resolve
   this with a spike (§6 item 2) before committing to the rest of the design.
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
4. **Scope creep toward option (b).** Once text messaging over a paired node
   works, "just add the LoRa radio directly" will look like a small step. It is
   not — §4 quantifies why. Treat it as a separate, later plan with its own
   go/no-go, not a natural continuation to fold into this milestone's cleanup.
5. **RadioLib version drift.** This plan's §2.2 findings are against the vendored
   7.1.2; a `lib_deps` bump could change what RadioLib does or doesn't expose
   without this plan being revisited. Since option (a) doesn't touch the LoRa
   radio at all, this mainly matters if/when option (b) is scoped.
6. **No second Meshtastic device to test against, in-repo.** Unlike Gadgetbridge
   (which has a fork of the actual Android app to test with, per CLAUDE.md), this
   plan has no equivalent "known-good other end" documented in this repo. Testing
   needs a real Meshtastic node (or the `meshtastic-python` CLI, which can run
   client-API-only against a node for reference/comparison) — budget for acquiring
   or borrowing one before implementation starts, not after.
