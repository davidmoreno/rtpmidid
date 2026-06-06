# Performance guidelines

Real-time MIDI I/O runs on the **poller thread** and **peer threads**. The
poller drives RTP timers and network reads — keep it fast.

## Avoid on the poller thread

UDP/ALSA read paths, timers, Avahi callbacks, DNS completion handlers:

- **Memory allocation** (`malloc`/`free`, `new`/`delete`, `vector::push_back` realloc)
- **Disk I/O** and **console I/O** (logging to stdout/stderr)
- **Blocking `getaddrinfo`** — use `dns_resolver_t` / `resolve_async()`
- **Allocating string ops** (`std::string` concat, `FMT::format` with heap)

Peer threads may block briefly on `snd_seq_drain_output()` or full UDP buffers
(`MSG_DONTWAIT` + drop on `EAGAIN` in `lib/udppeer.cpp`) — that only delays
that peer's queue.

## Best practices

- Stack buffers: `io_bytes_writer_static<N>` instead of dynamic allocation
- Pre-allocate with `reserve()` when size is known
- Use `mididata_t` as a view (no copy on hot path)
- Avoid logging in MIDI paths (`DEBUG` compiles out in release)
- Rate-limit hot-path warnings: `WARNING_RATE_LIMIT(seconds, ...)`

Good example (`lib/rtppeer.cpp`):

```cpp
void rtppeer_t::send_midi(const io_bytes_reader &events) {
  io_bytes_writer_static<4096 + 12> buffer;
  // ... build packet ...
  send_event(buffer, MIDI_PORT);
}
```

## Optional instrumentation

CMake option `RTPMIDID_ENABLE_TIMING` (`-DRTPMIDID_ENABLE_TIMING=1`) adds
`steady_clock` timestamps on `midi_packet_t` and per-packet timing logs in
`process_midi_packet`. Off by default.

## Areas needing review

1. **`midi_normalizer_t::m_buffer`** (`src/midi_normalizer.cpp`) — `vector::push_back` may reallocate on large SysEx
2. **`std::function` callbacks** — heap allocations; used in `normalize_stream()`, ALSA conversion helpers
3. **Logging in `send_midi()`** — use `ERROR_ONCE()` / `WARNING_RATE_LIMIT()`
4. **JSON status** — `status_rows()` is `O(N peers)` queue hops; fine for control plane, not MIDI path
5. **ALSA output** — one `snd_seq_drain_output()` per batch in `peer_device_alsa_seq.cpp`

## Related docs

- [midi-data-path.md](midi-data-path.md) — hot path data flow
- [event-loop.md](event-loop.md) — thread roles
- [concurrency.md](concurrency.md) — queue priorities
