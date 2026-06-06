# Development notes

Known limitations, extension recipes, and code conventions.

## Known limitations

1. **No journal support** — lost packets are not recovered (note off, CC, PC, pitch bend). Not production-ready on unreliable networks.
2. **No RTP timestamp usage** — events processed immediately on receipt; may affect timing over high-latency links.
3. **No JACK MIDI** — ALSA sequencer only.
4. **Throughput tuning** — many peer threads and fixed queue depths may need tuning at very high event rates.

Journal wire format: [RFC6295_notes.md](../reference/rfc6295-notes.md).

## Adding a new peer type

1. Create class inheriting `midipeer_t`
2. Implement all virtual methods
3. Register in `peer_factory.cpp` and `peer_kind.cpp`
4. Add identity prefix mapping and `device_identity_from_peer` helper
5. Write tests first (TDD) — see [testing.md](../development/testing.md)

Reference: [peer-types.md](../architecture/peer-types.md), [entities.md](../architecture/entities.md).

## Adding a new control command

1. Add params/result structs in `src/dm_json_rpc.hpp` with `// [dm-json]`
2. Rebuild (generator picks up types)
3. Add handler in `src/control_rpc.cpp` + `build_help_entries()`
4. Update [control-protocol.md](control-protocol.md)
5. Add test

Details: [dm-json.md](dm-json.md).

## Modifying the event loop

- Poller is a singleton — one instance only
- All `add_fd_in` FDs must be non-blocking (`MSG_DONTWAIT`, `SND_SEQ_NONBLOCK`)
- Use `call_later()` when deferring work off the current stack
- Timer/listener RAII: `timer_t`, `listener_t` auto-cancel on destruction

See [event-loop.md](../architecture/event-loop.md), [concurrency.md](../architecture/concurrency.md).

## Code style

- C++17
- `std::shared_ptr` for shared ownership
- RAII for resources (poller listeners, timers)
- Prefer `create_peer_from_string()` over direct construction
- `NON_COPYABLE_NOR_MOVABLE` for non-copyable types
- JSON via **dm-json** — no nlohmann

## External references

- [RFC 6295](https://tools.ietf.org/html/rfc6295) — RTP Payload Format for MIDI
- [Apple MIDI Network Driver Protocol](https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html)
- [RFC 4696](http://www.rfc-editor.org/rfc/rfc4696.txt) — RTP MIDI implementation guide

## Related docs

- [CONTRIBUTING.md](../../CONTRIBUTING.md) — build workflow
- [overview.md](../architecture/overview.md) — design principles
