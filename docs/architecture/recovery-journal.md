# Recovery journal (Chapter N) — specification

Status: **spec, not implemented**. Current code has a partial, incorrect
receive-side parser only. See [Status](#status-of-the-current-code).

Scope of this spec: **Chapter N** (MIDI NoteOff `0x8` / NoteOn `0x9`), sent
**and** received, always on, with the Apple-compatible `'RS'` feedback loop.
Chapter N is the chapter that repairs **missing NoteOn / NoteOff** — including
the stuck note caused by a lost NoteOff (the RFC's "indefinite artifact").

References:

- [RFC 6295](https://www.rfc-editor.org/rfc/rfc6295) §4, §5, Appendix A.1, A.6
  (normative wire format)
- [RFC 4696](https://www.rfc-editor.org/rfc/rfc4696) §7, §7.2 (non-normative
  sender/receiver algorithms)
- Apple MIDI Network Driver Protocol (journals always on, `'RS'` feedback,
  guard packets, which chapters Apple implements)
- Wire-format cheat sheet: [reference/rfc6295-notes.md](../reference/rfc6295-notes.md)
- Layer context: [rtp-midi-networking.md](rtp-midi-networking.md)
- Data path: [midi-data-path.md](midi-data-path.md)

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

## 3. Sender design (`recovery_journal_sender_t`)

State, all inside `rtppeer_t` and touched only from the peer's thread:

```cpp
struct note_state_t {
  uint8_t velocity;   // 0 == most recent N-active command was NoteOff
  uint32_t seq;       // extended seq of that command
  uint32_t order;     // monotonically increasing insertion order (oldest-first)
};
struct channel_state_t {
  note_state_t notes[128];      // N-active state
  uint16_t first_seq;           // extended seq of oldest entry in window
};
```

### 3.1 Sending a payload

`send_midi()` changes (lib/rtppeer.cpp):

1. Reserve the header octet; always OR in `J` (`0x40`).
2. Write the MIDI command section as today.
3. Append the journal section:
   - 3-octet top-level header with `C` = checkpoint for this packet (or `C = I`
     for an empty journal).
   - one channel journal per channel with content, ascending channel number.
   - `A=1`, `TOTCHAN = n-1`; `A=0, TOTCHAN=0` if empty.
4. Update sender state with the events just sent (`midi_out(seq_nr, events)`),
   including N-active resets.

Packet size: keep total ≤ MTU. Chapter N grows by 2 bytes per sounding note;
if the journal would exceed a cap (start with 512 bytes of journal, tune with
`tcpdump`), shrink the checkpoint window (raise `C`) and re-encode. A journal
with 128 note logs + OFFBITS is ~320 bytes worst case, so the cap only matters
for pathological multi-channel state.

### 3.2 Checkpoint selection (closed-loop)

- `ext_seq` is 32-bit (`seq_nr` + rollover counter); the wire carries the low
  16 bits.
- `ext_seq_confirmed` comes from `'RS'` feedback (§2.5).
- Default checkpoint: `C = ext_seq_confirmed` (the last packet the receiver
  says it saw), clamped to `[ext_seq_confirmed, I-1]` and never older than the
  oldest record still in the sender's window.
- If no feedback has ever arrived: checkpoint = first packet of the stream
  (anchor behaviour) — conforming, larger journals, self-corrects after the
  first feedback.
- Checkpoint must always satisfy `C <= I-1`; `C == I` means "empty journal".

### 3.3 Chapter N encoding rules

For each channel:

1. Collect notes whose most recent N-active command is inside `[C, I-1]`.
   Nothing else is coded.
2. Most recent command is a **NoteOn** (velocity ≠ 0) → note log; velocity of
   that NoteOn; ordered **oldest-first** by insertion order.
3. Most recent command is a **NoteOff** → set the OFFBITS bit for that note.
4. If both sets are empty → no channel journal for that channel.
5. `B` bit: 0 if packet `I-1` contained a NoteOff for this channel, else 1.
6. `S` bits: 0 for any element whose most recent coded command came from packet
   `I-1`, and then 0 for the channel journal and top-level header too. **This is
   not optional**: with `S=1` a receiver is allowed to skip the element in the
   single-packet-loss fast path (RFC 4696 §7), so a wrong `S=1` silently
   disables repair of the most common loss.
7. `Y` bit: 1 (play) for note logs of commands from packet `I-1`; for older
   entries use 0 (skip) when the note-on is clearly stale relative to the RTP
   timestamp of `I`, else 1. v1: always 1 for `I-1`, 0 for older.

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

For each note log (`n`, `velocity`, `Y`):

- `velocity == 0` → invalid per RFC 6295 (it is a NoteOff). Treated as the
  NoteOff it means, with a rate-limited warning.
- state is not sounding → a NoteOn (or NoteOn→NoteOff→NoteOn) was lost. Play it
  if `Y == 1`, skip it if `Y == 0`; either way update state as if executed.
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

### 4.4 Feedback and guard packets (poller side) — Phase 4

- Send `'RS'` feedback every ~1 s while connected (and immediately after the
  first packet of a stream), with the extended sequence number of the highest
  packet seen. Apple needs this to shrink its checkpoint; without it we also
  never stop its guard packets. `feedback_in()` already records the value the
  peer reports, so the sender side (Phase 3) can use it.
- Guard packets (ours): when MIDI activity stops and note state is non-empty,
  send journal-only packets at 100 ms, 200 ms, 400 ms, 800 ms, then 1 s
  (`guardtime`); stop when feedback confirms the receiver is caught up or MIDI
  activity resumes. Empty MIDI list, `J=1`, real journal.

### 4.5 Leaving a session — Phase 4

RFC 6295 §4: on exiting a session a receiver MUST ensure no indefinite
artifacts remain. On disconnect (including `BY`, timeout, socket error), emit
NoteOff for every note with `velocity != 0` (or CC 123 per affected channel).
`has_sounding_notes()` / `sounding_notes()` are already there for this, and the
state already knows about CC 120/123-127 and the Reset State commands (System
Reset and the GM/GM2/DLS SysEx messages), which clear it, because those commands
make earlier note commands not N-active (RFC 6295 Appendix A.1).

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
  void midi_played(const io_bytes_reader &events, uint32_t timestamp);
  void parse_journal(io_bytes_reader &, journal_loss_e loss, uint32_t timestamp,
                     signal_t<const io_bytes_reader &> &midi_out);
  bool has_sounding_notes() const;
  size_t sounding_notes() const;

  // Feedback (recorded in Phase 2, used by the sender in Phase 3)
  void feedback_in(uint32_t extended_seq);
  uint32_t confirmed_extended_seq() const;
  bool has_feedback() const;

  // Phase 3, sender
  // void midi_out(uint16_t seq_nr, const io_bytes_reader &events);
  // bool write_journal(io_bytes_writer &packet, uint16_t seq_nr);
  // Phase 4
  // void session_end(signal_t<const io_bytes_reader &> &out);

  bool enabled = true; // programmatic kill switch (Phase 3)
  stats_t stats;
};
}
```

Note the explicit `timestamp`: the receiver keeps the local execution time of each
NoteOn for the `Y`/staleness test, and taking it as a parameter keeps the class
free of a clock and the tests deterministic.

`rtppeer_t` owns one as the public member `recovery_journal`, so the daemon can
read `stats` without new plumbing.

Integration in `rtppeer_t` (no daemon changes needed for the core):

| Hook | Change | Phase |
|---|---|---|
| `parse_midi()` | `observe()` for loss classification, then `parse_journal()` emitting on `midi_event`, both before the packet's own events | **2, done** |
| `emit_midi()` | new private helper: every emitted event goes through `midi_played()` before reaching `midi_event` | **2, done** |
| `parse_feedback()` | read **uint32** at offset 8 (fixed), call `feedback_in()`, count it | **2, done** |
| `reset()` | `recovery_journal.reset()` | **2, done** |
| `send_midi()` | set `J`, call `midi_out()` while writing, `write_journal()` after | 3 |
| `send_feedback()` | periodic, with the highest sequence number seen; used by guard logic | 4 |
| `disconnect()` | `session_end()` before the status change | 4 |

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
  follow [mechanical-sympathy.md](mechanical-sympathy.md) (no allocation on the
  hot path; `io_bytes_writer_static` for packet building).
- The note/key state arrays are `16 * 128 * 12 B ≈ 24 KB` per peer; acceptable
  but note it in [performance.md](performance.md).
- The codec allocates a `std::vector` for the note logs of each decoded Chapter
  N. That is an allocation on the receive path (a few per second at most, and
  only for packets that carry a journal). If it shows up in profiles, keep one
  reusable `journal_message_t` per peer instead of building a new one per
  packet.

## 7. Status of the current code

**Phases 1 and 2 done.** `include/rtpmidid/journal.hpp` + `lib/journal.cpp`:

- `journal_codec_t`, the stateless wire codec (Phase 1), with 36
  golden-vector/round-trip tests in `tests/test_journal.cpp`.
- `recovery_journal_t`, the receiver state machine (Phase 2), with 21 tests in
  `tests/test_recovery_journal.cpp` and 3 integration tests in
  `tests/test_rtppeer.cpp`.

The old receive-side `parse_journal`, `parse_journal_chapter` and
`parse_journal_chapter_N` are **deleted**; incoming journals are handled by
`recovery_journal_t`. Sending still does nothing (`send_midi()` never sets `J`),
there is no periodic feedback and no guard packets yet.

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

Still to come:

- [ ] Oldest-first ordering with interleaved notes (Phase 3, sender state).
- [ ] Checkpoint clamping: no feedback (anchor), stale feedback, feedback ahead of
      what we sent (Phase 3).
- [ ] Sender: `J=1` on every payload, `S`/`B` derivation per element, MTU cap
      (Phase 3); periodic `'RS'` feedback, guard packets, note silencing on
      disconnect (Phase 4).

`tests/test_rtppeer.cpp` integration (Phase 2, done):

- [x] `test_journal_repairs_lost_note_off`: a NoteOn in sequence 0, sequence 1
      lost, sequence 2 with a journal → the peer emits the NoteOn and then the
      repaired NoteOff.
- [x] `test_journal_in_order_no_spurious_events`: the same journal one sequence
      number later emits nothing.
- [x] `test_feedback_reads_32_bit_sequence`: the `'RS'` extended sequence number
      is read (the old code read 16 bits and always got 0).

Pending:

- [ ] Feedback loop: receiver sends `'RS'`, sender's journal shrinks (Phase 3/4).
- [ ] Guard packet timing with a fake clock (Phase 4).
- [ ] Disconnect with notes held → NoteOff (or CC123) emitted (Phase 4).

Manual interop matrix (document results in the PR):

| Peer | Expectation |
|---|---|
| macOS / iOS Network MIDI (always `J=1`) | no duplicate/ghost notes; stuck notes from induced loss (100 ms of `tc netem` loss) recover |
| AppleMIDI Arduino library | still connects; journals ignored |
| Tobias Erichsen rtpmidi (Windows/Linux) | still connects and plays; check it does not mis-parse our journal |
| rtpmidid ↔ rtpmidid | full repair both directions |

## 9. Phases

- **Phase 0 — docs (done).** This spec; correct the wire-format errors in
  [reference/rfc6295-notes.md](../reference/rfc6295-notes.md); link from the
  docs index. No code.
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
- **Phase 3 — sender.** Next. `J=1` always, journal section in `send_midi`,
  closed-loop checkpoint from `'RS'`, `S`/`B` derivation, N-active resets, MTU
  cap. *Acceptance: our journal repairs a loss on our own receiver; checkpoints
  shrink after feedback.*
- **Phase 3 — sender.** `J=1` always, journal section in `send_midi`, closed-loop
  checkpoint from `'RS'`, `S`/`B` computation, N-active resets, MTU cap.
  *Acceptance: our journal repairs a loss on our own receiver; checkpoints
  shrink after feedback.*
- **Phase 4 — feedback + guard packets + session exit.** Periodic `'RS'`, guard
  schedule, note silencing on disconnect. *Acceptance: no stuck notes after a
  killed connection; guard packets observable in `tcpdump`.*
- **Phase 5 — observability + docs.** Counters → `rtp_peer_status_t` → JSON-RPC
  and Web UI; `docs/development/control-protocol.md`,
  [development-notes.md](../development/development-notes.md) (drop the "no
  journal support" limitation), [testing.md](../development/testing.md),
  `tests/README.md` checkboxes.

Each phase is a separate PR, with its doc update in the same PR (see
[AGENTS.md](../../AGENTS.md)).

## 10. Open questions

1. **Peer compatibility risk.** Always-`J=1` matches Apple and RFC 6295 ("every
   payload in a journaling stream MUST include a journal section"), but peers in
   the wild are only loosely conforming. If interop breaks with a specific peer,
   is a per-peer identity option (`rtpmidi_client:...,journal=off`) acceptable
   later, given the "always on, no config" decision?
2. **`Y` (play/skip) heuristic.** v1 proposes "play if it came from packet
   `I-1`, else skip". Tuning this needs real-device listening tests.
3. **Guard packet budget.** iOS/macOS devices on Wi-Fi: is 1 s guardtime too
   chatty? `guardtime` is not negotiated in Apple's handshake, so we must pick.
4. **Silence notes on disconnect (§4.5)** is technically outside Chapter N but
   is the same user-visible bug. Include in Phase 4 or a separate PR?
