# Concurrency primitives

Reference for the building blocks behind rtpmidid's actor model. Thread roles
and shutdown: [event-loop.md](event-loop.md).

## Actor model

The router and every peer are **actor-style**: each owns a thread, a
three-priority queue, and exclusive access to its mutable state. There is no
`peers_mutex` — every state read/write is dispatched via the queue.

Both mailboxes use `rtpmidid::blocking_priority_queue<Cmd, H, N, L>`: a
transport wrapper around lock-free `priority_mpsc_queue` with a
`condition_variable` for consumer wake-up. **`running_` flags live on the
actor** (`router_running_`, `thread_running_`), not on the queue.

Producers call `enqueue(item, prio)`; consumers loop on `running_` and call
`wait_dequeue(out)` (heartbeat timeout default 100ms). After loop exit, drain
remaining items on the same thread so reply channels never deadlock.

Actors: `midirouter_t`, each `midipeer_t`, `device_registry_t`.

### Router queue

`blocking_priority_queue<router_command_t, 4096, 256, 64>` — `std::variant` of
per-purpose typed structs (no generic lambdas).

| Priority | Commands |
|----------|----------|
| HIGH | `send_midi_t` |
| NORMAL | `add_peer_t`, `remove_peer_t`, `remove_all_peers_t`, `connect_t`, `disconnect_t`, `event_directed_t`, `event_broadcast_t`; `signal_peer_added_t`, `signal_connected_t`, `signal_disconnected_t`, `signal_peer_event_t`; `shutdown_t` |
| LOW | `query_peer_count_t`, `query_peer_ids_t`, `query_send_targets_t`, `query_get_peer_t`, `query_status_rows_t`, `for_each_peer_t`, `peer_connection_loop_t` |

### Peer queue

`blocking_priority_queue<peer_command_t, 1024, 64, 16>`:

| Priority | Commands |
|----------|----------|
| HIGH | `process_midi_t` |
| NORMAL | `shutdown_t` |
| LOW | `query_internal_latency_stats_t` |

### Reads and parallel stats

Synchronous APIs (`status_rows`, `peer_count`, `internal_latency_stats`, …)
enqueue typed `query_*_t` with `reply_slot_t{channel, id}` and block on
`reply_channel_t::wait(id)`.

`status_rows_impl()` issues latency queries to **every peer at once**, then
drains replies under `kStatusLatencyBudget`.

### Signals on the router thread

`connected_event`, `disconnected_event`, `peer_added_event`, `peer_event` fire
via typed `signal_*_t` messages on the router queue so listeners run on the
router thread after the current handler.

## priority_mpsc_queue

[`include/rtpmidid/priority_mpsc_queue.hpp`](../../include/rtpmidid/priority_mpsc_queue.hpp)

Lock-free multi-producer single-consumer ring buffer with three priority bands:

| Priority | Typical use |
|----------|-------------|
| HIGH | MIDI `send_midi` / `process_midi` |
| NORMAL | Topology changes, signals, shutdown |
| LOW | Status queries, latency stats |

Consumer drains HIGH → NORMAL → LOW. Producers never block (fixed capacity;
overflow is an error path).

Built on [`lockfree_queue.hpp`](../../include/rtpmidid/lockfree_queue.hpp).

## blocking_priority_queue

[`include/rtpmidid/blocking_priority_queue.hpp`](../../include/rtpmidid/blocking_priority_queue.hpp)

Transport wrapper around `priority_mpsc_queue`:

- Adds `condition_variable` + `mutex` for consumer wake-up
- `wait_dequeue(out)` — blocks until item or heartbeat timeout (default 100ms)
- `enqueue(item, prio)` — wakes one parked consumer
- Does **not** own thread lifecycle (`running_` flags live on the actor)

Queue depths (router): `4096 / 256 / 64`. Per peer: `1024 / 64 / 16`.

## reply_channel_t

[`include/rtpmidid/reply_channel.hpp`](../../include/rtpmidid/reply_channel.hpp)

Request-reply pattern for synchronous reads from actor threads:

1. Caller creates `shared_ptr<reply_channel_t>`, gets `id = next_id()`
2. Enqueues typed `query_*_t` with `reply_slot_t{channel, id}`
3. Blocks on `wait(id, timeout)`
4. Actor handler calls `channel->post(id, value)` or `post_error(id, msg)`

Replies for other ids stay queued for later waiters. Payload is `std::any`.

Used by: `midirouter_t::status_rows()`, `midipeer_t::internal_latency_stats()`,
`device_registry_t::list_devices()`, etc.

## signal_t / connection_t

[`include/rtpmidid/signal.hpp`](../../include/rtpmidid/signal.hpp)

Lightweight pub/sub for in-process events:

- `signal_t<Args…>::connect(callback)` → `connection_t` (RAII disconnect)
- `fire(args…)` invokes all slots

Router exposes: `connected_event`, `disconnected_event`, `peer_added_event`,
`peer_event`. These are **re-fired on the router thread** via typed
`signal_*_t` queue messages (not called directly from foreign threads).

## poller_t

[`include/rtpmidid/poller.hpp`](../../include/rtpmidid/poller.hpp),
[`lib/poller.cpp`](../../lib/poller.cpp)

Singleton `rtpmidid::poller` — Linux epoll main loop:

| Mechanism | Use |
|-----------|-----|
| `add_fd_in(fd, cb)` | UDP, ALSA seq, Avahi, DNS eventfd |
| `add_timer_event(ms, cb)` | CK timers, connection timeouts |
| `call_later(cb)` | Defer work off current call stack |
| `close()` | Exit main loop |

**Level-triggered** FD events. All registered FDs must be non-blocking.

Only the main/poller thread should run real-time I/O callbacks. See
[performance.md](performance.md).

## Shutdown pattern

Message-driven:

1. `stop_*_thread()` enqueues `shutdown_t` at NORMAL priority
2. Handler clears `running_` flag
3. Loop exits on next `wait_dequeue` timeout
4. Drain remaining queue items on the same thread

Fallback if queue full: flip flag + `queue_.wake()` directly.

Signal handling: `SIGINT`/`SIGTERM` → shutdown eventfd → `poller.close()`.
Worker threads mask signals via `block_shutdown_signals()`.

## Self-deadlock guard

Thread-local `g_current_router_thread` / `g_current_peer_thread`. Public actor
methods short-circuit to direct execution when called from the owning thread (or
in sync test mode with no thread running).

## Related docs

- [event-loop.md](event-loop.md) — thread inventory, shutdown order
- [database.md](database.md) — device_registry actor
- [rtp-midi-networking.md](rtp-midi-networking.md) — poller-driven network I/O
