# Core components

Reference for the main C++ types in the daemon. High-level overview:
[overview.md](overview.md).

## midirouter_t

Central hub: peer map, directed connections, MIDI routing, topology events.

**Responsibilities:**

- Maintains `peers` (`unordered_map<peer_id_t, shared_ptr<midipeer_t>>`)
- Unidirectional `connect(from, to)` / `disconnect`
- Routes MIDI from source to connected destinations (HIGH priority)
- Emits `connected_event`, `disconnected_event`, `peer_added_event`, `peer_event`

**Key methods** (thread-safe via queue + `reply_channel_t`):

```cpp
peer_id_t add_peer(std::shared_ptr<midipeer_t> peer);
void remove_peer(peer_id_t id);
void connect(peer_id_t from, peer_id_t to);
void disconnect(peer_id_t from, peer_id_t to);
void send_midi(peer_id_t from, const mididata_t& data);
void send_midi(peer_id_t from, peer_id_t to, const mididata_t&);
std::vector<router_peer_row_t> status_rows() const;
```

Aliases: `enqueue_send_midi`, `enqueue_connect`, `enqueue_disconnect`,
`enqueue_remove_peer`.

File: [`src/midirouter.cpp`](../../src/midirouter.cpp)

## midipeer_t

Abstract base for all MIDI endpoints.

**Per peer:**

- `peer_id` assigned by router
- Packet stats (`packets_sent`, `packets_recv`)
- Own actor thread + `peer_command_t` queue

**Virtual interface:**

```cpp
virtual router_peer_row_t status() const = 0;
virtual void send_midi(midipeer_id_t from, const mididata_t&) = 0;
virtual void event(midipeer_event_e event, midipeer_id_t from);
virtual bool control_peer_command(std::string_view cmd, std::string_view params_json,
                                  dmjson::writer_t& out, std::string& err);
virtual const char* get_type() const = 0;
virtual void on_router_attached() {}
```

**Helpers:** `enqueue_midi_packet()` (HIGH), `enqueue_to_router()`,
`internal_latency_stats()` (LOW query).

**Events:** `CONNECTED_ROUTER` / `DISCONNECTED_ROUTER`, `CONNECTED_PEER` /
`DISCONNECTED_PEER`.

File: [`src/midipeer.cpp`](../../src/midipeer.cpp). Peer kinds: [peer-types.md](peer-types.md).

## poller_t

Singleton epoll loop. See [concurrency.md](Concurrency.md#poller_t) and
[event-loop.md](event-loop.md).

## aseq_t

ALSA sequencer: create/destroy ports, read/write MIDI, connection events.
Sequencer FD registered on poller (non-blocking).

File: [`src/aseq.cpp`](../../src/aseq.cpp)

## mdns_rtpmidi_t

Avahi mDNS browse/publish for `_apple-midi._udp`. Remote handler creates
`alsa_listener` peers for discovered services.

File: [`lib/mdns_rtpmidi.cpp`](../../lib/mdns_rtpmidi.cpp). Flow:
[discovery.md](discovery.md).

## dns_resolver_t

Async hostname resolution:

```cpp
rtpmidid::dns_resolver().resolve_async(host, port, callback);
rtpmidid::dns_resolver_shutdown();  // before stopping peer threads
```

Worker thread runs `getaddrinfo`; completion wakes poller via `eventfd`.

Files: `include/rtpmidid/dns_resolver.hpp`, `lib/dns_resolver.cpp`

## Supporting services

| Component | Role | File |
|-----------|------|------|
| `control_socket_t` | Unix socket JSON-RPC server thread | `src/control_socket.cpp` |
| `device_registry_t` | Device list actor | `src/device_registry.cpp` |
| `connection_db_manager_t` | Persisted connection restore | `src/connection_db.cpp` |
| `web_server` | HTTP + WebSocket UI | `src/web_server.cpp` |
| `cron_tasks_t` | Periodic cleanup | `src/cron_tasks.cpp` |

## Related docs

- [component-interactions.md](component-interactions.md) — sequence diagrams
- [entities.md](entities.md) — devices and identities
- [database.md](database.md) — persistence layer
