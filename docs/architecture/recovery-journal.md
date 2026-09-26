# Recovery journal (Chapter N) — specification

Status: **spec, not implemented**. Current code has a partial, incorrect
receive-side parser only. See [Status](#status-of-the-current-code).

Scope of this spec: **Chapter N** (MIDI NoteOff `0x8` / NoteOn `0x9`), sent
**and** received, always on, with the Apple-compatible `'RS'` feedback loop.
Chapter N is the chapter that repairs **missing NoteOn / NoteOff** — including
the stuck note caused by a lost NoteOff (the RFC's "indefinite artifact").

References:

- [RFC 6295](https://www.rfc-editor.org/rfc/rfc6295) §4, §5, Appendix A.1, A.6
  (normative wire format; local copy: [rfcs/rfc6295.txt](rfcs/rfc6295.txt))
- [RFC 4696](https://www.rfc-editor.org/rfc/rfc4696) §7, §7.2 (non-normative
  sender/receiver algorithms; local copy: [rfcs/rfc4696.txt](rfcs/rfc4696.txt))
- [RFC 3550](https://www.rfc-editor.org/rfc/rfc3550) §6.4 (RTCP report blocks and
  the EHSNR field named below; local copy: [rfcs/rfc3550.txt](rfcs/rfc3550.txt))
- Apple MIDI Network Driver Protocol (journals always on, `'RS'` feedback,
  guard packets, which chapters Apple implements)
- Wire-format cheat sheet: [RFC6295_notes.md](../RFC6295_notes.md)
- Layer context: `rtp-midi-networking.md` (not in this branch's docs tree)
- Data path: `midi-data-path.md` (not in this branch's docs tree)

## Decisions (locked)

| Decision | Choice |
|---|---|
| Scope | Both directions, **Chapter N only**; other chapters designed for but stubbed |
| Sending policy | **Always `J=1`** + **closed-loop** checkpoints driven by `'RS'` feedback + guard packets |
| Configuration | **No INI knob** — journaling is always on. A programmatic `enable_journal()` flag exists for tests/library users |
| Observability | Counters on `rtppeer_t`, exposed through `rtp_peer_status_t` (JSON-RPC + Web UI) |
| Delivery | This spec, then phased implementation (see [Phases](#9-phases)) |

## 1. Why Chapter N

RFC 6295 §4 classifies rendering artifacts:

- **Transient**: a lost NoteOn → one note fails to play, artifact ends there.
- **Indefinite**: a lost NoteOff → the note sounds forever; a lost CC7 → wrong
  volume for the rest of the session.

The **recovery journal mandate** is: *the rendered performance MUST NOT contain
indefinite artifacts*. The mechanism is not retransmission — every payload
carries a "journal" section describing commands between the **checkpoint
packet** `C` and the previous packet `I-1`, so a receiver that lost packets can
diff that description against its own state and repair.

| Chapter | Protects | Indefinite artifact? |
|---|---|---|
| **N** | NoteOn `0x9`, NoteOff `0x8` | **Yes (stuck notes)** — this spec |
| E | overlapping NoteOns, release velocity | No (supplements N) |
| C | Control Change `0xB` (volume, pan, pedal) | Yes |
| W | Pitch Wheel `0xE` | Yes |
| P | Program Change `0xC` | Yes |
| M / T / A | parameter system, channel/poly aftertouch | No |
| D / V / Q / F / X | system commands | Varies |

Chapter N has two halves, and **each fixes one direction of the problem**:

- **Note log list** (NoteOn side): a lost NoteOn = missing note on. The list
  codes the most recent N-active NoteOn per note number, plus a `Y` hint to
  play or skip it.
- **NoteOff bitfield** (NoteOff side): a lost NoteOff = missing note off /
  stuck note. A set bit means "there may be a NoteOff you never saw".

## 2. Wire formats (normative)

All values are big-endian. `I` is the packet carrying the journal, `C` the
checkpoint packet.

### 2.1 Top-level journal header — 3 octets

```
  0                   1                   2
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |S|Y|A|H|TOTCHAN|   Checkpoint Packet Seqnum    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Bits | Meaning |
|---|---|---|
| `S` | 0 | Single-packet-loss hint. **1** by default; MUST be 0 if any element in this journal codes a command stored in packet `I-1` (and then every containing element and this header MUST also be 0) |
| `Y` | 1 | System journal present (we never set: no system chapters in scope) |
| `A` | 2 | Channel journals present |
| `H` | 3 | Enhanced Chapter C encoding. Always 0 for us |
| `TOTCHAN` | 4-7 | Number of channel journals **minus one** — i.e. `TOTCHAN+1` channel journals follow |
| Checkpoint seqnum | 8-23 | 16-bit sequence number of packet `C` |

Coverage rule: the journal covers the loss event if
`checkpoint_seq <= (highest_received_seq + 1) mod 2^16`.

An empty journal (only the header, `A=0 Y=0`) is legal and required in every
payload of a journaling stream even when there is nothing to report.

### 2.2 Channel journal — 3 octets + chapters

```
  0                   1                   2
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |S| CHAN  |H|      LENGTH       |P|C|M|W|N|E|T|A|
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Bits (octet0) | Meaning |
|---|---|---|
| `S` | `0x80` | S-bit for this channel journal |
| `CHAN` | `(b0 >> 3) & 0x0F` | MIDI channel 0-15 (same encoding as the MIDI status nibble) |
| `H` | `0x04` | Enhanced Chapter C |
| `LENGTH` | `((b0 & 0x03) << 8) \| b1` | Total octets of this channel journal **including these 3 header octets** and all chapters |

`LENGTH` is a skip field: receivers MUST use it to advance past a channel
journal they do not fully understand, never internal-format knowledge
(RFC 6295 Appendix A.1).

TOC bits (octet2): `P=0x80, C=0x40, M=0x20, W=0x10, N=0x08, E=0x04, T=0x02,
A=0x01`. Chapters appear in TOC order. Channel journals appear in ascending
channel order; one per channel at most.

### 2.3 Chapter N

```
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |B|     LEN     |  LOW  | HIGH  |S|  NOTENUM   |Y|  VELOCITY    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |S|  NOTENUM   |Y|  VELOCITY    |            ....               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |    OFFBITS    |    OFFBITS    |     ....      |    OFFBITS    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Header (2 octets):

| Field | Meaning |
|---|---|
| `B` | S-bit functionality for the NoteOff bitfield. **1** by default; MUST be 0 if packet `I-1`'s MIDI section contains a NoteOff for this channel (then all containing S bits MUST be 0) |
| `LEN` | Number of 2-octet note logs that follow. `0` = empty note list. `LEN=127` with `LOW=15,HIGH=0` codes **128** logs; other `LEN=127` codes 127 |
| `LOW`, `HIGH` | First/last OFFBITS **octet index**. If `LOW <= HIGH`, `HIGH-LOW+1` OFFBITS octets follow. `(15,0)` and `(15,1)` code an **empty** NoteOff bitfield. Other `LOW > HIGH` pairs MUST NOT be emitted |

Note log (2 octets each, **oldest-first ordering**):

| Field | Meaning |
|---|---|
| `S` | S-bit for this log (`0x80` of octet 0) |
| `NOTENUM` | 7-bit note number, `0x7F` mask |
| `Y` | `0x80` of octet 1: hint — 1 = play the recovered NoteOn, 0 = skip it |
| `VELOCITY` | 7-bit velocity of the most recent N-active NoteOn. **Never zero** (zero-velocity NoteOn is a NoteOff and lives in OFFBITS) |

OFFBITS octets:

- Each octet codes 8 consecutive notes; **MSB = lowest note of the group**.
- MSB of the first octet = note `8*LOW`; MSB of the last = note `8*HIGH`.
- Set bit = a NoteOff command for that note number.
- NoteOff velocity is not coded.
- A note number MUST NOT appear in both the note list and OFFBITS.
- Emit the most efficient encoding: first and last octets contain at least one
  set bit.

### 2.4 N-active state (when note state is invalidated)

A note command stops being "N-active" if it appears **before** any of these in
the session history (RFC 6295 Appendix A.1):

- Control Change `120` (All Sound Off), `123`-`127` (All Notes Off family)
- Reset State commands: System Reset `0xFF`, and the GM/GM2/DLS SysEx resets

Both sender and receiver MUST clear per-note journaling state on these
commands. Receivers still pass the command itself through to ALSA.

### 2.5 `'RS'` receiver feedback — 12 octets, control port

```
0xFFFF | 'RS' | SSRC (4) | most recently received seq num (4)
```

Community implementations (Wireshark dissector, mt32-pi, midimonster, Apple's
documented "journal feedback packet") agree on a **32-bit** sequence field. The
value is the extended sequence number; the low 16 bits are the RTP sequence
number, the high 16 bits a rollover count used to normalise it.

This is what lets a peer shrink its checkpoint window (closed-loop policy) and
stop sending guard packets. We must send it periodically (see §4.4) — today we
only send it as a side effect of parsing a journal, with a wrong value.

## 3. Sender design — **implemented (Phase 3)**

`recovery_journal_t` keeps, per MIDI channel and note number, the most recent
N-active command:

```cpp
struct sender_note_t {
  uint32_t extended_seq; // extended sequence number of that command
  uint32_t order;        // session history order, for oldest-first encoding
  uint8_t velocity;      // 0 means the most recent command was a NoteOff
  bool has_command;
};
```

The 32-bit extended sequence number is `(rollovers << 16) | seq_nr`, tracked in
`midi_out()` so the 16-bit wrap of the RTP sequence number is handled.

### 3.1 Sending a payload

1. `send_midi()` writes the header with `J=1` (when `enabled`), the MIDI command
   section as usual, and then appends the journal: the 3-octet top-level header,
   plus one channel journal per channel with content, in ascending channel
   order. An empty journal (header only, `A=0`) is still written: RFC 6295 §2.2
   requires a journal section in every payload of a journaling stream.
2. Then `midi_out(seq_nr, events)` records what was sent, assigning each command
   the packet's extended sequence number and a session-history order.
3. The journal is therefore written **before** the packet's own commands are
   recorded. That ordering is what makes the S bits correct: a journal codes the
   history `C..I-1`, which never contains packet `I` (RFC 6295 Appendix A.1).

### 3.2 Checkpoint selection

- With no `'RS'` feedback yet, the policy is **anchor**: checkpoint = the first
  packet of the stream, so the history always covers the whole session and
  repair works without any feedback. Journals are larger, and shrink as soon as
  feedback arrives.
- `feedback_in()` normalizes the peer's reported extended sequence number to our
  own rollover count (RFC 3550 half-range rule) and keeps the maximum, because a
  peer reports the *highest* packet it has seen: stale or reordered reports must
  not pull the checkpoint back.
- The checkpoint is clamped to `[first packet, I-1]`.
- On the very first packet the checkpoint is the packet itself (`C == I`), which
  means "empty history".

### 3.3 Chapter N encoding rules

For each channel:

1. Code only notes whose most recent command is inside `[C, I-1]`.
2. Most recent command is a **NoteOn** → note log, `VELOCITY` from that command,
   ordered **oldest-first** by the recorded history order (commands in the same
   packet share a sequence number, so a counter is needed to break ties).
3. Most recent command is a **NoteOff** → set the OFFBITS bit for that note.
4. Nothing to code for a channel → no channel journal at all.
5. `S` bit of a note log: 0 when the command came from packet `I-1`. `B` bit:
   0 when packet `I-1` contained a NoteOff on that channel. The channel journal
   S bit and the top-level S bit are **derived** from the elements by the codec,
   so they can never disagree with them.
6. `Y` bit: 1 for a note log that codes a command from packet `I-1` (the RFC's
   "simultaneous with the packet"), 0 for older entries, which a receiver should
   skip rather than retrigger. This is what keeps a sustained note from being
   re-attacked every time the journal is repeated.
7. N-active resets: CC 120, CC 123-127 and Reset State commands (System Reset
   and the GM/GM2/DLS SysEx) clear the channel state, so commands before them are
   never coded again.

### 3.4 Packet size

The journal is capped at `recovery_journal_t::max_journal_size` (512 octets) and
at the space left in the packet buffer. If it does not fit, the checkpoint
**advances** until it does, which drops the oldest commands: that trades repair
coverage for packet size, which is exactly what a sending policy is about. With
the default cap this only triggers with roughly a hundred notes sounding at once.
If even the newest history does not fit, the journal is sent anyway with a
rate-limited warning.

### 3.5 Known limits of this phase

- Chapter N only: a lost CC 7 (volume), program change or pitch bend is still
  not repaired. This includes CC 123 (All Notes Off), which clears our state, so
  a lost CC 123 cannot be recovered (Chapter C territory).
- Skipped (`Y=0`) recovered NoteOns are transient artifacts, which RFC 6295 §4
  explicitly allows; the mandate only covers indefinite artifacts.

## 4. Receiver design (`recovery_journal_t`) — **implemented (Phase 2)**

State (the RFC 4696 "RJRS"): per MIDI channel, per note number:

```cpp
struct note_state_t {
  uint32_t extended_seq; // extended sequence number of the most recent command
  uint32_t time;         // local time of the most recent NoteOn
  uint8_t velocity;      // 0 means "not sounding"
};
```

All of it is updated by `midi_played()`, which is told about every MIDI event the
peer actually emits, plus by the repairs themselves. Nothing else touches it.

### 4.1 Loss detection

- Track the extended sequence number of the highest packet seen.
- On each MIDI payload: `delta = int16_t(seq - last_seq)` (so wrap-around works).
  - `delta <= 0` → out-of-order or duplicate: play it, **do not** repair.
  - `delta == 1` → in order.
  - `delta == 2` → single-packet loss.
  - `delta > 2` → multi-packet loss.
- **Repair only when a gap was detected.** This is the critical behavioural
  change: Apple always sends `J=1`, so a receiver that unconditionally applies
  journals (the previous code) replays already-played NoteOns and emits spurious
  NoteOffs on every packet.
- The first packet of a stream is classified `none`, deliberately deviating from
  RFC 6295 §4 ("process the first received packet as if it were a packet that
  ends a loss event"). With an empty RJRS there is nothing to compare against,
  so applying the journal would only replay notes from the checkpoint window:
  the ghost note problem in reverse. Revisit once our sender's `Y` policy is in
  place.

### 4.2 Fast paths

- `A == 0` → nothing to do.
- Single-packet loss and top-level `S == 1` → the lost packet had an empty MIDI
  list: nothing to do.
- Single-packet loss → skip channel journals with `S == 1`, note logs with
  `S == 1`, and the OFFBITS structure when `B == 1`.
- Multi-packet loss → parse everything.

### 4.3 Chapter N repair (RFC 4696 §7.2)

Process **OFFBITS first**, then note logs, both bounded by the channel journal's
`LENGTH`.

For each set OFFBITS bit (note `n`):

- if `velocity != 0` → a NoteOff (or NoteOff→NoteOn→NoteOff) was lost: emit
  `NoteOff n` on that channel, clear the state. **This is what unsticks a
  stuck note.** A set bit for a note we do not believe is sounding is ignored,
  so an anchor-policy peer repeating its journal cannot silence anything twice.
  It is not silent though: the bit says the note was N-active at the
  checkpoint, so there is a NoteOn we never received, and a rate-limited
  WARNING says so.

For each note log (`n`, `velocity`, `Y`):

- `velocity == 0` → invalid per RFC 6295 (it is a NoteOff). Treated as the
  NoteOff it means, with a rate-limited warning.
- state is not sounding → a NoteOn (or NoteOn→NoteOff→NoteOn) was lost. Play it
  if `Y == 1`, skip it if `Y == 0`; either way update state as if executed. This
  is the journal telling us about a note we did not know was sounding, so it
  gets a rate-limited WARNING: it is the normal single-loss repair, but it is
  also how a peer whose state has diverged from ours shows up.
- state is sounding → test for a lost NoteOff→NoteOn sequence:
  - `velocity != log.velocity` → lost sequence
  - recorded sequence is before the journal checkpoint → lost sequence
  - `Y == 1` and the recorded NoteOn is older than
    `note_on_recent_window` (250 ms) → lost sequence
  → emit `NoteOff n`, then play/skip the logged NoteOn per `Y`, and update state
  as if executed.

The 250 ms window is the only heuristic here; it is the tunable knob if real
devices show either retriggered sustained notes or missing notes.

Emitted repairs go out on the **same `midi_event` signal** as normal MIDI, so the
daemon routes them to ALSA with no extra plumbing, and they are emitted before
the events of the packet that carried the journal, which is the correct order.

### 4.4 Feedback and guard packets — **implemented (Phase 4)**

`journal_timer_t` (one per peer, created by `rtpclient_t` and
`rtpserverpeer_t`) runs on the poller thread and does two things, both only while
the peer is `CONNECTED`:

- **Receiver feedback**: on the first tick after connecting and then every
  `feedback_period` (1 s), send an `'RS'` packet on the control port with the
  extended sequence number of the highest packet received. That is what lets the
  peer shrink its checkpoint history and stop its own guard packets.
- **Guard packets**: while `sender_has_pending_state()` is true (we recorded note
  commands the peer has not confirmed) and nothing has been sent for
  `guard_min_period`, send a journal-only packet, backing off
  `guard_min_period → guard_max_period` (100 ms → 1 s, the RFC's guardtime). No
  packet is sent while real MIDI is flowing.

**When the episode ends.** The first guard of an episode is the *anchor*: its
journal is the one that carries the pending note state, since the guards after it
are retransmissions of the same content (RFC 4696 §4.2: guard packets contain no
new MIDI information, only recovery journals). The episode is therefore over as
soon as `sender_confirmed(guard_anchor)` — the peer's feedback covers that
packet, even if it never acknowledges the newest retransmission. Waiting for
`sender_is_caught_up()` instead does not terminate against a peer whose report
lags behind us: every guard we send is itself a new packet to confirm, so the
target moves away exactly as fast as we retransmit. That was the endless
guard-packet loop seen with real hardware (a 1 s guardtime and an `'RS'` report
that is structurally 1–2 packets behind). The anchor stays set while the same
note state is pending, so the episode is not reopened until new MIDI arrives or
the peer catches up completely.

Guard packets do not fire `midi_sent_event`, so they never count as MIDI activity
and cannot reset their own backoff.

### 4.5 Leaving a session — **implemented (Phase 4)**

RFC 6295 §4: on exiting a session a receiver MUST ensure no indefinite artifacts
remain. `recovery_journal_t::session_end()` emits a NoteOff for every note it
believes is sounding, and `rtppeer_t::reset()` calls it, which covers every
teardown path: `BY`, connect failures, timeouts, socket errors and destruction.
`stats.notes_silenced` counts them.

That is also how a stuck note from a network death is cleaned up locally, without
waiting for any journal, and it uses the same state that tracks CC 120/123-127 and
the Reset State commands (System Reset, GM/GM2/DLS SysEx), which clear it because
they make earlier note commands not N-active (RFC 6295 Appendix A.1).

## 5. Library API

New files, both LGPL like the rest of `lib/`:

```
include/rtpmidid/journal.hpp
lib/journal.cpp
```

The file is split in two layers so each phase is testable on its own:

1. **`journal_codec_t`** (Phase 1, implemented): stateless encode/decode of the
   journal section, the channel journal header, Chapter N, plus size and
   skip/step-over arithmetic. The PODs it moves around are `journal_header_t`,
   `journal_channel_header_t`, `journal_note_log_t`, `journal_chapter_n_t`,
   `journal_channel_t` and `journal_message_t`. It throws `bad_journal` for
   content that cannot be expressed or that is malformed.
2. **`recovery_journal_t`** (receiver in Phase 2, sender in Phase 3): the session
   state machine on top of the codec.

The receiver API, as implemented:

```cpp
namespace rtpmidid {

enum class journal_loss_e { none, single, multi };

class recovery_journal_t {
public:
  struct stats_t {
    // Receiver (Phase 2)
    uint32_t journals_received = 0, notes_repaired_on = 0,
             notes_repaired_off = 0, notes_skipped = 0, losses = 0,
             out_of_order = 0, malformed = 0;
    // Sender (Phase 3/4)
    uint32_t journals_sent = 0, guard_packets = 0, feedback_sent = 0,
             feedback_received = 0;
  };

  /// A NoteOn older than this (0.1 ms units) is "clearly not recent" for the
  /// Y bit test of RFC 4696 section 7.2.
  static constexpr uint32_t note_on_recent_window = 2500; // 250 ms

  void reset();

  // Receiver
  journal_loss_e observe(uint16_t seq_nr);
  size_t session_end(signal_t<const io_bytes_reader &> &midi_out);
  uint32_t highest_received_extended_seq() const;
  bool has_received_packet() const;
  void midi_played(const io_bytes_reader &events, uint32_t timestamp);
  void parse_journal(io_bytes_reader &, journal_loss_e loss, uint32_t timestamp,
                     signal_t<const io_bytes_reader &> &midi_out);
  bool has_sounding_notes() const;
  size_t sounding_notes() const;

  // Sender
  void midi_out(uint16_t seq_nr, const io_bytes_reader &events);
  size_t write_journal(io_bytes_writer &writer);
  bool sender_has_pending_state() const;
  bool sender_is_caught_up() const;

  // Feedback: from the 'RS' packet, drives the sender checkpoint
  void feedback_in(uint32_t extended_seq);
  uint32_t confirmed_extended_seq() const;
  bool has_feedback() const;

  static constexpr size_t max_journal_size = 512;
  bool enabled = true; // programmatic kill switch
  stats_t stats;
};
}
```

Note the explicit `timestamp`: the receiver keeps the local execution time of each
NoteOn for the `Y`/staleness test, and taking it as a parameter keeps the class
free of a clock and the tests deterministic.

`rtppeer_t` owns one as the public member `recovery_journal`, so the daemon can
read `stats` without new plumbing.

The poller-side timers live in `journal_timer_t`
([include/rtpmidid/journal_timer.hpp](../../include/rtpmidid/journal_timer.hpp)),
one instance per peer, created by `rtpclient_t` and `rtpserverpeer_t` and started
by the peer's `CONNECTED` status. Periods (`tick_period`, `feedback_period`,
`guard_min_period`, `guard_max_period`) are public members, so tests can shrink
them.

Integration in `rtppeer_t` (no daemon changes needed for the core):

| Hook | Change | Phase |
|---|---|---|
| `parse_midi()` | `observe()` for loss classification, then `parse_journal()` emitting on `midi_event`, both before the packet's own events | **2, done** |
| `emit_midi()` | new private helper: every emitted event goes through `midi_played()` before reaching `midi_event` | **2, done** |
| `parse_feedback()` | read **uint32** at offset 8 (fixed), call `feedback_in()`, count it | **2, done** |
| `reset()` | `recovery_journal.reset()` | **2, done** |
| `send_midi()` | set `J`, append `write_journal()` after the MIDI section, then `midi_out()` | **3, done** |
| `send_feedback()` | periodic, with the highest sequence number seen (`journal_timer_t`) | **4, done** |
| `reset()` | `session_end()` before forgetting the note state | **4, done** |

Daemon-side follow-ups (Phase 5): counters into `rtp_peer_status_t`
(`src/dm_json_status.hpp`, `src/utils.cpp`), regenerate dm-json goldens
(`make test-gen`), document in `docs/development/control-protocol.md` and
optionally surface in the Web UI monitor.

## 6. Threading

`rtppeer_t` is already touched from two threads: the poller thread
(`data_ready` → `midi_event`, `status_change_event`) and the peer actor thread
(`send_midi`). Today's `seq_nr` / `seq_nr_ack` / `remote_seq_nr` already rely on
this being mostly benign. The journal adds more shared state, so:

- Keep all journal mutation on the poller thread **and** the peer thread path
  as today (no new thread, no locks) — matching every other `rtppeer_t` field.
  In practice the receiver state (`observe`, `midi_played`, `parse_journal`) is
  only touched from `rtppeer_t::data_ready()`, i.e. the poller thread.
- Counters are read by `status_rows` on another thread: use relaxed
  `std::atomic<uint32_t>` for the `stats_t` fields so reads cannot tear, and
  follow the mechanical-sympathy notes (no allocation on the
  hot path; `io_bytes_writer_static` for packet building).
- The journal state is `16 * 128 * 12 B ≈ 24 KB` (receiver) plus
  `16 * 128 * 16 B ≈ 32 KB` (sender) per peer, so about 56 KB, plus the reused
  `coded_channels_` vector (allocated once, on the first packet that has a
  journal to code). Worth a performance note.
- The codec allocates a `std::vector` for the note logs of each decoded Chapter
  N. That is an allocation on the receive path (a few per second at most, and
  only for packets that carry a journal). If it shows up in profiles, keep one
  reusable `journal_message_t` per peer instead of building a new one per
  packet.

## 7. Status of the current code

**Phases 1 to 5 done.** The library is feature complete for Chapter N:

- `journal_codec_t`, the stateless wire codec (Phase 1), with 36
  golden-vector/round-trip tests in `tests/test_journal.cpp`.
- `recovery_journal_t`: receiver (Phase 2), sender (Phase 3) and session exit
  (Phase 4), with tests in `tests/test_recovery_journal.cpp`.
- `journal_timer_t`: periodic `'RS'` feedback and guard packets (Phase 4), with
  `tests/test_journal_timer.cpp` driving it through the real poller.
- Integration tests in `tests/test_rtppeer.cpp`, including a two-peer
  send-and-repair test where a packet is dropped on the way.
- Counters (Phase 5): `recovery_journal_t::stats` reports, per peer, journals
  received/sent, notes repaired/skipped/silenced, losses, out-of-order packets,
  malformed journals, guard packets and feedback, exercised by
  `tests/test_recovery_journal.cpp` and `tests/test_rtppeer.cpp`. Exposing them through the daemon JSON-RPC
  surface and the Web UI is a separate change and **is not part of this
  library-side port**.

The old receive-side `parse_journal`, `parse_journal_chapter` and
`parse_journal_chapter_N` are **deleted**; incoming journals are handled by
`recovery_journal_t`. `send_midi()` sets `J=1` and appends a journal to every
packet. What is left is on the wire protocol side: chapters other than N.

Defects 1-9 below are fixed by construction (the codec replaced that code);
10 is Phase 4. Kept here as the record of what was wrong:

| # | Defect |
|---|---|
| 1 | Channel journal header parsed with overlapping masks: `LENGTH = ((b0 & 0x07) << 8)` (swallows `H`), `CHAN = (b0 & 0x70) >> 4`. Correct: `((b0 & 0x03) << 8)`, `(b0 >> 3) & 0x0F` |
| 2 | Loops `TOTCHAN` channels, not `TOTCHAN+1` |
| 3 | `LENGTH` is a total-size skip field but is used as a remainder; `skip(length)` overshoots by 3. Unknown chapters (`P/C/M/W/T/A`) abort the journal instead of being skipped |
| 4 | OFFBITS decoding never adds bit index `j` (`tmp[1] = minnote`) → all set bits emit the same wrong note number |
| 5 | `S`/`B` bits ignored → no single-packet-loss fast path |
| 6 | **No loss detection**: journals are applied unconditionally, so with an always-`J=1` Apple peer every packet replays NoteOns and spurious NoteOffs |
| 7 | No per-note receiver state → cannot distinguish "already played" from "missed" |
| 8 | `Y` bit always means "play"; no local state to skip stale notes |
| 9 | `parse_feedback` reads `uint16` at offset 8 where Apple writes `uint32` |
| 10 | No periodic feedback, no guard packets, no note silencing on disconnect |
| 11 | Unmerged prior art on `origin/journal` (`lib/journal.cpp`, 2021) has its own bugs: NoteOn/NoteOff seq arrays swapped in `midi_in()`, writer stores OFFBITS LSB-first while its reader reads MSB-first, `LOW/HIGH >> 4` instead of `>> 3`, note logs ordered by note number instead of oldest-first, no N-active resets. Reuse the structure, not the code |

## 8. Test plan

Unit (`tests/test_journal.cpp`, TDD — expectations first). **Done in Phase 1**
(36 tests, all passing):

- [x] Codec golden vectors, byte-exact: empty journal; one note log; OFFBITS
      single bit; multi-octet OFFBITS; `LOW=15,HIGH=0` empty bitfield; `(15,1)`
      empty bitfield; 128 note logs (`LEN=127`); 127 note logs plus OFFBITS;
      `S`/`Y`/`B` bit coding; channel and top-level header vectors.
- [x] Round-trip: encoded journal → parser → identical structures, including a
      deterministic sweep over every note number and both structures.
- [x] `TOTCHAN+1` channel journals, ascending order enforcement.
- [x] `LENGTH` skipping: `P`/`C`/`M`/`W` chapters before `N` do not corrupt the
      `N` parse; chapters after `N` are skipped; a leading system journal (`Y=1`,
      as Apple sends for sequencer state and MTC) is stepped over.
- [x] `S`/`B` bit rules: a command from packet `I-1` ⇒ element/channel/header
      `S=0` (top-level `S` is derived, not trusted from the caller).
- [x] Rejections: zero-velocity note log, note in both structures, duplicate
      note log, more than 128 logs, 128 logs with OFFBITS, invalid `LOW/HIGH`,
      truncated chapter, channel `LENGTH` overrun, descending channels.
- [x] Malformed chapter inside a multi-channel journal is skipped and flagged
      (`journal_message_t::malformed`) without losing the next channel.

Receiver state machine (`tests/test_recovery_journal.cpp`, 21 tests). **Done in
Phase 2**:

- [x] Sequence classification: in order, single, multi, duplicate, reordered,
      wrap-around at `0xFFFF`, and a fresh stream after `reset()`.
- [x] Receiver idempotence: Apple-style `J=1` on every packet with **no loss**
      emits **zero** extra events (regression guard for defect 6), and does not
      even parse the journal.
- [x] Receiver repair: single-packet loss of NoteOff → one NoteOff emitted, state
      cleared; single-packet loss of NoteOn → NoteOn or skip per `Y`; multi-packet
      loss parses everything; `NoteOff→NoteOn` detection via velocity, checkpoint
      and stale `Y`/time, each tested on its own.
- [x] Fast paths: `S=1` at the top level, per-channel `S`, per-log `S`, `B=1`.
- [x] Out-of-order packet → no repair.
- [x] N-active resets: CC 120 and 123-127, System Reset `0xFF`, the GM/GM2/DLS
      Reset State SysEx (with and without leading `F0`), and non-reset CCs/SysEx
      left alone.
- [x] Invalid zero-velocity note log treated as a NoteOff.
- [x] Channel isolation: a repair on one channel never touches another.
- [x] A set OFFBITS bit for a note we do not believe is sounding is ignored.

Sender (`tests/test_recovery_journal.cpp`). **Done in Phase 3**:

- [x] Empty journal for the first packet, and a journal code for a note that was
      turned on in packet `I-1` (S=0, Y=1) with byte-exact expectations.
- [x] Oldest-first note log ordering, independent of note number, with the S/Y
      bits of each entry.
- [x] Anchor checkpoint without feedback, and the checkpoint moving forward with
      `'RS'` feedback, including ignoring stale reports.
- [x] N-active resets (CC 123) clearing the sender state.
- [x] Packet size cap advancing the checkpoint until the journal fits.
- [x] `enabled = false` writing nothing.
- [x] End to end: a two-peer test (`test_rtppeer.cpp`) where packet 1 is dropped
      and the receiver emits the repaired NoteOff between the two NoteOns.

Feedback, guard packets and session exit (`tests/test_journal_timer.cpp` and
`tests/test_recovery_journal.cpp`). **Done in Phase 4**:

- [x] The timer sends receiver feedback with the highest sequence number
      received, and guard packets (journal only, `J=1`, empty MIDI list) with
      backoff while the peer has not confirmed our stream.
- [x] Guards stop once the peer's feedback covers the first guard of the
      episode (`sender_confirmed(guard_anchor)`), while feedback keeps flowing.
      Regression: `test_guards_stop_when_the_peer_report_lags_behind` uses a peer
      whose `'RS'` is always one packet behind, and which never confirms the
      newest guard.
- [x] The timer stops when the peer disconnects.
- [x] `session_end()` emits a NoteOff for every sounding note, counts them, and is
      a no-op when nothing is sounding.

Observability. **Counters are in the library (Phase 5)**; the daemon RPC and Web
UI surface are not part of this library-side port:

- [x] `recovery_journal_t::stats` counts repairs, losses, skipped and silenced
      notes, guard packets and feedback after a real loss and repair.
- [ ] Exposing them in `rtp_peer_status_t` and JSON-RPC (`router.status`,
      `peer.status`, `router.peer_updated`) and the Web UI Peers table —
      separate change, not included here.

Still to come:

- [ ] Chapter C (volume, All Notes Off), which Chapter N cannot protect.
- [ ] The guard-delay tuning above, once counters from real devices are in.
- [ ] The manual interop matrix, still pending.

`tests/test_rtppeer.cpp` integration (Phase 2, done):

- [x] `test_journal_repairs_lost_note_off`: a NoteOn in sequence 0, sequence 1
      lost, sequence 2 with a journal → the peer emits the NoteOn and then the
      repaired NoteOff.
- [x] `test_journal_in_order_no_spurious_events`: the same journal one sequence
      number later emits nothing.
- [x] `test_feedback_reads_32_bit_sequence`: the `'RS'` extended sequence number
      is read (the old code read 16 bits and always got 0).

- [x] Receiver sending periodic `'RS'` feedback, and the sender's journal
      shrinking because of it (Phase 4).
- [x] Guard packet timing, on the real poller instead of a fake clock (Phase 4).
- [x] Disconnect with notes held → NoteOff emitted (Phase 4).

Manual interop matrix (document results in the PR):

| Peer | Expectation |
|---|---|
| macOS / iOS Network MIDI (always `J=1`) | no duplicate/ghost notes; stuck notes from induced loss (100 ms of `tc netem` loss) recover |
| AppleMIDI Arduino library | still connects; journals ignored |
| Tobias Erichsen rtpmidi (Windows/Linux) | still connects and plays; check it does not mis-parse our journal |
| rtpmidid ↔ rtpmidid | full repair both directions |

## 9. Phases

- **Phase 0 — docs (done).** This spec; correct the wire-format errors in
  [RFC6295_notes.md](../RFC6295_notes.md); link from the library README. No code.
- **Phase 1 — codec (done).** `include/rtpmidid/journal.hpp` + `lib/journal.cpp`
  with Chapter N encode/decode, top-level and channel headers, and skip/size
  arithmetic; 36 golden-vector/round-trip tests in `tests/test_journal.cpp`. Not
  wired into `rtppeer_t` yet. *Acceptance met: byte-exact vectors; round-trip;
  `LENGTH`/`TOTCHAN`/`S`/`B` semantics covered.*
- **Phase 2 — receiver (done).** Seq/loss tracking, RJRS state, RFC 4696 §7.2
  repair on top of `journal_codec_t::read_journal()`, the old `parse_journal*`
  deleted from `rtppeer.cpp`, `parse_feedback` reading `uint32`. *Acceptance met:
  loss integration tests in `test_rtppeer.cpp`; zero extra events with an
  always-`J=1` peer.*
- **Phase 3 — sender (done).** `J=1` always, journal section in `send_midi`,
  anchor/closed-loop checkpoint from `'RS'`, `S`/`B` derivation, oldest-first
  logs, N-active resets, packet size cap. *Acceptance met: a two-peer test drops
  a packet and the receiver repairs it; `test_sender_feedback_shrinks_the_
  checkpoint` covers the checkpoint moving with feedback.*
- **Phase 3 — sender.** `J=1` always, journal section in `send_midi`, closed-loop
  checkpoint from `'RS'`, `S`/`B` computation, N-active resets, MTU cap.
  *Acceptance: our journal repairs a loss on our own receiver; checkpoints
  shrink after feedback.*
- **Phase 4 — feedback + guard packets + session exit (done).** `journal_timer_t`
  sends periodic `'RS'` feedback and backs off guard packets; `session_end()`
  silences sounding notes on every teardown path. *Acceptance met by
  `test_journal_timer.cpp` (feedback and guard packets on the real poller, guards
  stopping when the peer is caught up, timer stopping on disconnect) and by the
  `session_end` tests; the `tcpdump` part remains for manual validation.*
- **Phase 5 — observability + docs (library part done).** Counters live in
  `recovery_journal_t::stats`; the daemon JSON-RPC surface
  (`rtp_peer_status_t::journal` → `router.status`, `peer.status`,
  `router.peer_updated`) and the Web UI Peers table (Journal column with a
  tooltip breakdown) are a separate change and are **not** part of this
  library-side port. *Counters are exercised by the `test_recovery_journal.cpp`
  and `test_rtppeer.cpp` assertions on `recovery_journal.stats`.*

Each phase is a separate PR, with its doc update in the same PR.

## 10. Open questions

1. **Peer compatibility risk.** Always-`J=1` matches Apple and RFC 6295 ("every
   payload in a journaling stream MUST include a journal section"), but peers in
   the wild are only loosely conforming. If interop breaks with a specific peer,
   is a per-peer identity option (`rtpmidi_client:...,journal=off`) acceptable
   later, given the "always on, no config" decision? `recovery_journal_t::enabled`
   already exists as the programmatic switch.
2. **`Y` (play/skip) heuristic.** Implemented as "play only what came from packet
   `I-1`", which follows the RFC's definition of the bit and avoids retriggering
   sustained notes, at the cost of skipping a note whose NoteOn was lost several
   packets back (a transient artifact, allowed by RFC 6295 §4). Tuning this needs
   real-device listening tests.
3. **Guard packet budget.** iOS/macOS devices on Wi-Fi: is 1 s guardtime too
   chatty? `guardtime` is not negotiated in Apple's handshake, so we must pick.
4. **Silence notes on disconnect (§4.5)** is technically outside Chapter N but
   is the same user-visible bug. Include in Phase 4 or a separate PR?
5. **First-guard delay: tune it with real data (deferred, decision made).** The
   100 ms comes from RFC 4696 §4.2's informational reference algorithm, not from
   a mandate: the only protocol parameter is `guardtime` (maximum separation,
   typical 500-2000 ms, which we keep as `guard_max_period`), and Apple's
   handshake has no SDP, so nothing is negotiated. The plan once Phase 5 counters
   have produced data from real devices:
   - drive the first delay from **jitter**, not mean latency: what the delay must
     exceed is the *differential* delay between the original packet and the guard,
     otherwise the guard wins the race, the receiver repairs, and the late
     original then plays the NoteOn a second time (audible double trigger). Mean
     latency is the wrong quantity; `peer.stats.average_and_stddev().stddev`
     (CK samples) is the signal we already have.
   - `guard_min_period = clamp(2 × stddev, 50 ms, 250 ms)`, falling back to
     100 ms until enough CK samples exist. The floor stops guard spam on a LAN
     with ~0 latency and ~0 loss; the cap keeps worst-case stuck-note repair
     under ~350 ms.
   - use a one-shot timer armed after the last MIDI packet instead of the 100 ms
     tick, so a target below the tick period is actually honoured (today the
     effective delay is 100-200 ms) and there are fewer wake-ups.
   - the counters to watch first: `out_of_order` (guards racing delayed packets)
     and `notes_repaired_on` versus `notes_skipped` (whether `Y` is tuned right).
6. **Timers are per peer.** `journal_timer_t` adds a timer per peer; with many
   peers that is many wake-ups. If it shows up in profiles, one shared timer could
   drive all peers. The one-shot guard timer above removes most of them.
7. **Chapter C next?** A lost CC 7 (volume) or CC 123 (All Notes Off) is an
   indefinite artifact that Chapter N cannot repair, and CC is cheap to add
   (fixed 2-octet logs). It is the natural follow-up once Chapter N has been
   validated against real devices.
8. **Interop validation is still pending.** Everything here is tested against
   itself and against the RFC byte layouts; the manual matrix below needs to run
   on real macOS/iOS/Windows peers.
