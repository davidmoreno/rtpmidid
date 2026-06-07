# AGENTS.md — developer index

Minimal entry for AI agents and contributors. **Do not duplicate long-form docs
here** — update the linked files under `docs/`.

Human users start at [README.md](README.md) → [docs/user/](docs/user/).

Full index: [docs/README.md](docs/README.md).

## Project

Userspace daemon bridging ALSA MIDI ↔ RTP-MIDI (`src/`, GPLv3) plus
**librtpmidid** (`lib/`, `include/rtpmidid/`, LGPL 2.1).

## Key source files

| Area | Files |
|------|-------|
| Entry / shutdown | `src/main.cpp` |
| Router / peers | `src/midirouter.cpp`, `src/midipeer.cpp`, `src/peer_*.cpp` |
| Factory / spawn | `src/peer_factory.cpp`, `src/peer_spawn.cpp` |
| Identity / DB | `src/device_identity.cpp`, `src/connection_db.cpp`, `src/device_registry.cpp` |
| Control / Web | `src/control_rpc.cpp`, `src/control_socket.cpp`, `src/web_server.cpp` |
| Stats collector | `src/stats_collector.cpp`, `src/event_subscription.cpp` |
| Event loop / RTP | `lib/poller.cpp`, `lib/rtppeer.cpp`, `lib/rtpclient.cpp` |
| JSON | `lib/dm_json/runtime.cpp`, `src/dm_json_*.hpp` |

## Documentation map

| Topic | Doc |
|-------|-----|
| Architecture overview | [docs/architecture/overview.md](docs/architecture/overview.md) |
| Devices / peers / connections | [docs/architecture/entities.md](docs/architecture/entities.md) |
| Peer types | [docs/architecture/peer-types.md](docs/architecture/peer-types.md) |
| Threads / poller | [docs/architecture/event-loop.md](docs/architecture/event-loop.md) |
| Actor queues | [docs/architecture/concurrency.md](docs/architecture/concurrency.md) |
| Mechanical sympathy | [docs/architecture/mechanical-sympathy.md](docs/architecture/mechanical-sympathy.md) |
| INI (full) | [docs/development/configuration.md](docs/development/configuration.md) |
| JSON-RPC | [docs/development/control-protocol.md](docs/development/control-protocol.md) |
| dm-json | [docs/development/dm-json.md](docs/development/dm-json.md) |
| Tests / build | [CONTRIBUTING.md](CONTRIBUTING.md), [docs/development/testing.md](docs/development/testing.md) |

## Conventions

- C++17, `shared_ptr`, RAII; peers via `create_peer_from_string()`
- Router edges are **unidirectional** (bidirectional = two edges)
- Actor threads + typed queue messages — [docs/architecture/concurrency.md](docs/architecture/concurrency.md)
- Do not block the poller thread — [docs/architecture/performance.md](docs/architecture/performance.md)
- Identity: `type:key=value,...` — [docs/architecture/entities.md](docs/architecture/entities.md)
- JSON via dm-json only — [docs/development/dm-json.md](docs/development/dm-json.md)

## Documentation maintenance policy

**When you change code, update the matching doc in the same PR.**

| Change | Update |
|--------|--------|
| New / removed RPC method | `docs/development/control-protocol.md`, `build_help_entries()` in `control_rpc.cpp`, [docs/user/control.md](docs/user/control.md) if user-facing |
| INI section or key | `docs/development/configuration.md`, [docs/user/configuration.md](docs/user/configuration.md), `default.ini` |
| New peer type or identity prefix | `docs/architecture/peer-types.md`, `peer_kind.cpp`, `peer_factory.cpp` |
| Device / connection / DB behavior | `docs/architecture/entities.md`, `docs/architecture/database.md` |
| Web UI tab or flow | `docs/user/web-ui.md`, `docs/development/frontend.md` |
| Thread / queue / shutdown | `docs/architecture/event-loop.md`, `docs/architecture/concurrency.md` |
| dm-json struct or generator | `docs/development/dm-json.md`, regenerate goldens if needed (`make test-gen`) |
| User-visible breakage or common failure | [docs/user/troubleshooting.md](docs/user/troubleshooting.md) |

Do **not** grow AGENTS.md with new prose — add or extend files under `docs/`.
Keep [docs/README.md](docs/README.md) index in sync when adding doc files.

Remove stale terms (`listen_rtpmidi`, `[bridge]`, `stats` RPC, split
`router.create.*`) if found in docs.

## External references

- [RFC 6295](https://tools.ietf.org/html/rfc6295)
- [Apple MIDI Network Driver Protocol](https://developer.apple.com/library/archive/documentation/Audio/Conceptual/MIDINetworkDriverProtocol/MIDI/MIDI.html)
