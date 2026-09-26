# Agent notes

Working notes for coding agents (and humans) in this repo. Keep it short and
factual: only things that are not obvious from the code.

## Build and test

```sh
make build          # or: cd build && cmake --build . -j8
cd build && ctest --output-on-failure
clang-format --dry-run -Werror <files>   # .clang-format at the root
```

`-DLOG_LEVEL=1` is set for the tests, so `DEBUG(...)` compiles; the runtime level
starts at `INFO`, so pass `--log-level debug` (daemon) or call
`rtpmidid::logger2.set_log_level(rtpmidid::logger_level_t::DEBUG)` to see them.

## Local RFC copies — do not re-fetch

The specs this project depends on are committed under `docs/rfcs/`. **Grep and
read these first**; do not download them again:

| File | What it is |
|---|---|
| `docs/rfcs/rfc6295.txt` | RTP-MIDI payload format (normative wire format, journal section, Appendix A/B) |
| `docs/rfcs/rfc4696.txt` | Implementation guide: sender/receiver journal algorithms, guard packets, EHSNR trimming |
| `docs/rfcs/rfc3550.txt` | RTP/RTCP: report blocks, EHSNR definition (§6.4.1), RTCP interval rules (§6.2, §6.3.1) |

If another RFC is needed, `curl -o docs/rfcs/rfcNNNN.txt
https://www.rfc-editor.org/rfc/rfcNNNN.txt`, add it to that table, and use the
local copy from then on. Non-RFC references (Apple's MIDI Network Driver
Protocol) stay as URLs; quote the passage in the note that needs it.

## RTP-MIDI facts that bit us

- **There is no per-packet ACK.** The only confirmation is the periodic receiver
  feedback: Apple's `'RS'` packet (`0xFFFF 'RS' SSRC(4) seq32(4)`, control port),
  or RTCP RR's EHSNR in standard RTP-MIDI. Both mean "highest packet received",
  nothing more.
- **This project implements only the Apple flavour**: `'RS'` in, `'RS'` out.
  There is no RTCP/EHSNR code at all (decision: for real hardware it buys little,
  see the guard-loop discussion below; RTCP alone would not make us an RFC 6295
  endpoint anyway — no SDP, no standard session layer).
- **Apple-style port layout**: control = base port, MIDI data = base + 1
  (`lib/rtpserver.cpp`). Standard RTP/RTCP would use base + 1 for RTCP.
- **Guard packets are real stream packets**: they consume RTP sequence numbers,
  contain no MIDI, only the journal (RFC 4696 §4.2), and they must not fire
  `midi_sent_event`.
- **Guard termination uses the episode anchor**, not the newest packet: a guard
  episode ends when the peer's feedback covers the *first* guard of the episode
  (`sender_confirmed(guard_anchor)`), because the later ones are retransmissions
  of the same content. Requiring the newest packet to be confirmed never
  terminates with a peer whose report lags (1 s guardtime vs ~1 s `'RS'`), which
  is what was observed with a Roland JUPITER-Xm: guard packets retransmitted at
  guardtime for as long as a note stayed pending.
- Guard packets *do* advance `last_packet_seq_`, so `sender_is_caught_up()` can
  legitimately stay false for a long time. Use `sender_confirmed(seq)` when the
  question is "did this particular journal reach the peer?".
- A peer that reports only packets that carry MIDI (ignoring guards) can never be
  confirmed as caught up; the anchor rule cannot help there and retransmitting at
  guardtime is the safe behaviour.

## Code and test conventions

- `signal_t::connect()` returns a `connection_t` that disconnects on
  destruction. **Always store it** in a member or local that outlives the
  subscription; dropping it silently unsubscribes (cost me one confusing debug
  session).
- `poller.add_timer_event()` is **one-shot**. Callbacks must re-arm themselves,
  and the returned `timer_t` must live somewhere real: a discarded temporary
  cancels the timer immediately, and a `shared_ptr` captured by its own callback
  keeps it armed after the owner is gone (use-after-free).
- Tests use `tests/test_case.hpp` (`TEST`, `ASSERT_*`) and `tests/test_utils.hpp`
  (`hex_to_bin`, `poller_wait_for`, `poller_wait_until`). Packet literals read
  like `"FF FF 'RS' 0000 0000 00 00 12 34"`.
- Proving a fix: temporarily re-introduce the old behaviour, watch the new test
  fail, then restore. Regression tests that never failed are not worth much.

## Docs to keep in sync

`docs/architecture/recovery-journal.md` is the living spec for the journal
(§4.4 for the guard timer): update it when the behaviour changes, and keep the
`tests/...` checklist entries true.
