# Plan: show the phone's SMS history in the Chats tab

Today the Chats tab shows messages the watch *witnessed*: an SMS becomes visible
only if Android raised a notification for it while the watch was connected and
`gb_messages` decided it looked message-like. Everything else — the thread from
last week, the messages that arrived while the watch was on the charger, the
ones already read on the phone — does not exist as far as the watch is
concerned.

Making the tab show all text messages is therefore not a UI change. It is a new
capability that needs the phone to *read its own SMS database* and a way to move
that database across a link that currently moves 20 bytes at a time. The UI work
is the last and smallest part.

This plan covers the whole path. It assumes `watch-settings-sync-protocol-plan.md`'s
conventions for adding a message pair and does not re-derive framing, the
handler-interface pattern, or how the Gadgetbridge fork is built — see
`.claude/twatch-ultra-ble-protocol.md` §2 and §10, and
`.claude/android-notifications-gadgetbridge-plan.md` phases 2–4.

## 1. Where things stand (verified 2026-09-10)

**Watch store.** `gadgetbridge_ble/gb_messages.{h,cpp}` turns the `notify`
stream into conversations. `GbMessageStore::ingest()` is only ever called from
`GbApp`'s notification path, and the store is capped at
`GB_MAX_CONVERSATIONS` (8) × `GB_MAX_MESSAGES_PER_CONVERSATION` (12) — 96
messages total, oldest evicted. Both are `#ifndef` so a build flag can raise
them, but they are sized for a live alert feed, not an archive.

**Identity.** `GbMessage::id` is the *notification* id. It is what §6.6 replies
and `notify-` dismissals key on. A message read out of the SMS provider has no
notification id, which is the single most consequential fact in this plan (§7).

**Threading.** `threadKey()` groups on app + `tel`/`sender`/`title`.
`isTextMessage()` guesses from an app-name list when the phone sends neither
`tel` nor `sender` — the comment in `gb_messages.cpp` already flags this as a
guess awaiting a `type` field.

**Protocol.** `notify` (§5.3) is push-only. There is no request/response
anywhere in the protocol, no cursor, no history, and no phone→watch message
that carries more than one item. Adding one is explicitly sanctioned: §10 says
unknown `t` values are logged and dropped on both sides, so a new type is
backwards compatible in both directions.

**Hardware.** The Ultra has 8 MB QSPI PSRAM (`BOARD_HAS_PSRAM` in
`boards/lilygo-t-watch-ultra.json`) and `src/factory/partitions.csv` gives it an
~8.25 MB `ffat` partition with `board_build.filesystem = fatfs`. Static RAM is
at 60 KB of 320 KB. **Storage on the watch is not a constraint** — a 100 000-SMS
archive at ~120 bytes each is 12 MB, and even that fits flash if not RAM.

**Transport.** This is the constraint. §2: "Outgoing writes are split into
20-byte chunks regardless of the negotiated MTU." Lines over 8192 bytes are
discarded.

## 2. What "all text messages" can and cannot mean

**SMS and MMS: yes.** Android exposes `content://sms` and `content://mms` to any
app holding `READ_SMS`. Columns `_id`, `thread_id`, `address`, `body`, `date`,
`type` (1 = inbox, 2 = sent) give everything the Chats tab renders.

**Signal, WhatsApp, Telegram, Threema: no, and this will not change.** Their
stores are app-private and encrypted; there is no content provider to query.
Gadgetbridge sees those conversations only as notifications, which is exactly
what the watch already gets. No amount of work on either side produces history
for them.

So the honest scope is: **SMS/MMS gain full history; everything else keeps
today's live-only behaviour.** The Chats tab ends up showing two kinds of thread
with different depth, and that difference should be visible in the UI (§8)
rather than looking like data loss.

**Decision needed before Phase 1.** Confirm SMS/MMS-only is the intent. If the
goal was "every conversation in full", the answer is that it is not achievable
and the plan should stop here.

## 3. The link is the bottleneck, not the watch

At 20-byte writes, throughput is set by how many writes fit in a connection
interval. Assuming one write-with-response per 30 ms interval, that is ~660 B/s.
Even with write-without-response and several per interval, budget 3–8 KB/s.

An SMS as a JSON object with `tel`, `body`, `date` and a direction flag runs
~120–300 bytes. So:

| Corpus | Bytes | At 660 B/s | At 5 KB/s |
| --- | --- | --- | --- |
| 200 recent messages | ~50 KB | 75 s | 10 s |
| 5 000 messages | ~1.2 MB | 30 min | 4 min |

Three levers, in the order they are worth pulling:

1. **Raise the chunk size in the fork.** The 20-byte split is the *phone's*
   behaviour and you own that code. Negotiating an MTU and chunking at MTU−3
   (typically 244) is a ~12× improvement for every message type, not just this
   feature. **Verify first whether the chunking lives in Gadgetbridge core or in
   the device support class** — if core, changing it affects every other device
   and belongs upstream, not in a fork patch.
2. **Batch.** One `sms` message carrying an array of N messages amortises the
   JSON envelope and the per-line overhead. Cap the batch so the serialised line
   stays under 8192 bytes (§2) — roughly 25–50 messages, and the phone must
   measure rather than assume.
3. **Window, then backfill.** Sync the most recent N messages (or last 30 days)
   first so the tab is useful in seconds, then continue in the background with a
   resumable cursor. Never block the UI on a full sync.

## 4. Protocol additions

Three messages. Field names stay short, consistent with the existing set.

### 4.1 `smsreq` — watch asks for history (watch → phone, §6.9)

| Field | Type | Meaning |
| --- | --- | --- |
| `n` | string | `sync` (fetch a page) or `cancel` |
| `before` | int | Cursor: return messages with `date` strictly older than this (ms epoch). Omit for "newest first". |
| `max` | int | Page size hint; the phone may return fewer |

```json
{"t":"smsreq","n":"sync","before":1757462400000,"max":40}
```

Cursor-by-date rather than by offset: the corpus grows while syncing, and an
offset would skip or repeat messages when it does.

### 4.2 `sms` — a page of history (phone → watch, §5.15)

| Field | Type | Meaning |
| --- | --- | --- |
| `m` | array | Messages, newest first |
| `m[].i` | int | Provider `_id`, stable identity for dedup |
| `m[].th` | int | Provider `thread_id` |
| `m[].tel` | string | Address (normalised by the phone) |
| `m[].nm` | string | Contact display name, if resolvable |
| `m[].b` | string | Body |
| `m[].d` | int | Date, ms epoch |
| `m[].o` | bool | True if outgoing (provider `type` 2) |

```json
{"t":"sms","m":[{"i":8812,"th":42,"tel":"+15551234567","nm":"Ada","b":"On my way","d":1757462390000,"o":false}]}
```

### 4.3 `smsend` — page boundary / sync state (phone → watch, §5.16)

| Field | Type | Meaning |
| --- | --- | --- |
| `more` | bool | True if older messages remain |
| `next` | int | Cursor to pass as `before` in the next `smsreq` |
| `err` | string | Optional: `noperm`, `unavailable` |

`err":"noperm"` is how the watch learns the user declined `READ_SMS`, so it can
say so instead of showing an empty tab and a spinner forever.

## 5. Gadgetbridge side

1. **Permission.** `READ_SMS` is a dangerous permission requiring a runtime
   grant. Request it lazily — on first `smsreq`, not at app start — and handle
   denial by answering `smsend` with `err":"noperm"`. (Play Store policy
   restricts `READ_SMS`, which is moot for a self-built Codeberg fork but would
   block upstreaming.)
2. **Query.** `ContentResolver.query()` on `content://sms` with a
   `date < ?` selection, `ORDER BY date DESC LIMIT ?`. Resolve `address` to a
   contact name via `ContactsContract.PhoneLookup` — cache it, the lookup is
   per-row otherwise.
3. **Normalise the number** with `PhoneNumberUtils.normalizeNumber()` so the
   watch's dedup (§7) compares like with like. The same contact appears as
   `+15551234567`, `5551234567` and `(555) 123-4567` across providers.
4. **Batch and measure.** Serialise until the line approaches 8192 bytes, then
   flush and continue. Do not compute a fixed N.
5. **MMS is a second pass.** `content://mms` bodies live in `content://mms/part`
   and need a per-message query; attachments have no representation in this
   protocol. Defer to a later phase and send MMS as a text placeholder or skip
   it entirely at first.
6. **Live SMS should use the same path.** Whatever `SmsReceiver` hook pushes a
   new SMS should emit it with the same `i`/`d` identity so a live arrival and
   the same message later seen in history dedup cleanly (§7).

## 6. Watch-side storage

`GbMessageStore` stays as it is — it is the *live notification* store and its
caps are right for that job. History goes in a second store behind the same
accessor, rather than growing one class to do both.

**Tier 1 — PSRAM working set.** Allocate the history store's backing memory from
PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` or a custom allocator on the
`std::vector`s). This is what makes "hold a few thousand messages" a non-issue.
Do not put it on the default heap: 320 KB is shared with NimBLE, LVGL and the
Wi-Fi stack when that comes up.

**Tier 2 — `ffat` persistence.** Without it every reboot re-syncs the whole
corpus over a 660 B/s link. Write one append-only file per `thread_id` plus a
small index holding the newest `_id` and `date` seen. On boot, load the index and
`smsreq` only what is newer — turning a 30-minute cold sync into a 2-second
delta. This is the single highest-value item after the protocol itself.

**Ordering.** The wire sends newest-first (natural for a cursor walking
backwards); `GbConversation::messages` is oldest-first. Insert accordingly
rather than sorting the whole thread on every page.

## 7. Merging live and history — the hard part

The same SMS can reach the watch twice: once as a `notify` while connected, once
as an `sms` history row. They must collapse into one message.

**Identity.** The provider `_id` (`m[].i`) is authoritative and stable.
Notification-derived messages have no `_id`, so they need a fallback key:
normalised `tel` + `date` rounded to the nearest second + a hash of the body.
When a history row arrives whose fallback key matches an existing
notification-derived message, **replace** it and adopt the `_id` — the history
row is the better record.

**Reply routing is the real problem.** §6.6 `reply` requires the `id` of a
`notify` message. History rows do not have one, so a thread rebuilt purely from
history cannot be replied to under the current protocol. Two ways out:

- **Preferred:** extend §6.6 to accept `id":0` when `tel` is present, and have
  the fork send via `SmsManager` rather than through the notification's remote
  input. This is a small additive change and makes replies work uniformly.
- Fallback: mark history-only threads read-only in the UI. Worse, and the
  inconsistency ("why can I reply to this thread but not that one") will be a
  recurring bug report.

Settle this in Phase 1, because it changes the protocol.

## 8. UI

Smaller than it looks — the Chats tab's shape does not change.

- **`refreshChats()` rebuilds every row on every change.** With 8 threads that is
  fine; with 200 it is not. Move to building rows lazily, or cap the visible list
  and rely on the existing preview truncation.
- **Thread view needs upward paging.** `refreshThread()` renders all messages;
  a 2000-message thread cannot. Render the newest ~50 and load older on scroll.
- **Sync state must be visible.** A progress row ("syncing 1 200 / 5 000") on the
  Chats tab, and a distinct empty state for `err":"noperm"` that says the
  permission was declined rather than "No messages".
- **Distinguish shallow threads.** A Signal thread has no history and never
  will; showing it identically to a fully-synced SMS thread invites "where did my
  messages go". A caption on the thread view is enough.
- New sizes go in `app_config.h` under the existing `APP_GB_*` block.

## 9. Step order

Each phase leaves the tree working.

| Phase | Work | Done when |
| --- | --- | --- |
| 0 | Confirm §2 scope; verify where the 20-byte chunking lives; settle §7 reply routing | Decisions written into this file |
| 1 | Protocol spec into `.claude/twatch-ultra-ble-protocol.md` §5.15/5.16/6.9; no code | Both sides agree on the wire |
| 2 | Fork: permission, provider query, `smsreq`/`sms`/`smsend`; larger chunks if §3.1 allows | Pages observable in `logcat` |
| 3 | Watch: parse + PSRAM history store + dedup. **No UI change.** | Driven end-to-end in the emulator via `gb_link_stdio` |
| 4 | Watch: Chats/thread paging, sync indicator, permission-denied state | Usable on hardware |
| 5 | `ffat` persistence + delta sync on boot | Second boot syncs in seconds |
| 6 | Reply-by-`tel`; MMS bodies | Replies work on history-only threads |

Phase 3 is where the emulator earns its keep: `gb_link_stdio.cpp` takes protocol
lines on stdin, so a synthetic corpus can be piped in and the store, the dedup
and the caps exercised with no phone, no BLE and no waiting on 660 B/s.

## 10. Testing

- **Synthetic corpus** piped into the emulator: 5 000 messages across 40 threads,
  including duplicate `_id`s, out-of-order dates, empty bodies, emoji, and a
  4 000-character SMS.
- **Dedup:** deliver the same message as a `notify` then as an `sms` row; assert
  one message and that it adopted the `_id`.
- **Resume:** kill the link mid-sync; assert the next `smsreq` resumes from the
  stored cursor and does not restart.
- **Permission denied:** `smsend` with `err":"noperm"`; assert the tab says so.
- **Line cap:** a batch that serialises past 8192 bytes must be split by the
  phone, not discarded by the watch.
- **Memory:** log PSRAM free before and after a full sync; assert the default
  heap is untouched.

## 11. Open risks

- **Throughput may make full sync impractical** if §3.1 turns out to be
  unchangeable without patching Gadgetbridge core. Mitigation: the windowed sync
  in §3.3 still gives a useful tab; "all messages" becomes "all recent messages".
- **`READ_SMS` blocks upstreaming** the device support to Gadgetbridge proper.
  Fine for a personal fork; worth knowing before investing.
- **Dedup on a fuzzy key** (§7) will occasionally be wrong for two identical
  bodies to the same number in the same second. Rare and low-harm, but real.
- **Battery.** A 30-minute BLE sync at a short connection interval is not free on
  either device. Sync on charge, or throttle when the watch is below
  `APP_LOW_BATT_DEFAULT_PCT`.
- **`gb_messages`' app-name guessing** (`isMessagingApp()`) stays a guess for
  non-SMS apps. Unrelated to this plan but adjacent; the `type` field its comment
  asks for would be a cheap addition to §5.3 while the protocol is open.
