# Mechanical sympathy

Rules for writing performance-sensitive code in rtpmidid.

## Hot paths

The **router MIDI path** threads (`handle(send_midi_t)`, `send_midi_inline`)
is the single hottest code in the daemon — every MIDI byte passes through it.
Allocations, mutex acquisitions, and syscalls in this path degrade throughput
for every participant.

| What                                    | Allowed in hot path? | Reason                                 |
| --------------------------------------- | -------------------- | -------------------------------------- |
| `std::mutex::lock`                      | ❌                    | Blocks the router thread               |
| `new` / `malloc`                        | ❌                    | Heap contention, unpredictable latency |
| Map lookup (hash)                       | ❌                    | Multiple cache misses                  |
| `std::function` call through null check | ✅                    | ~2ns if null, ~10ns if set             |
| Atomic load/store (relaxed)             | ✅                    | ~1–3ns, no contention                  |
| SPSC `lockfree_queue::enqueue`          | ✅                    | Lock-free, 1 CAS, pre-allocated ring   |

## Stats collector architecture

`src/stats_collector.cpp` — `stats_collector_t` owns **all** peer packet counters.

```
Router MIDI path (hot)               Collector thread (cold)
─────────────────────                ───────────────────────
if (on_peer_sent)                    
  on_peer_sent(peer_id)              
    queue_.enqueue({pid, true}) ──→  drain batch
  (SPSC, lock-free, no alloc)        increment alignas(64) atomics
                                     every 200ms: emit events to subscribers
```

Key properties:
- **Hot path**: single SPSC `enqueue()` — a compare-and-swap on the producer's
  cache line. The consumer's cache line is untouched at enqueue time.
- **No false sharing**: `lockfree_queue` uses `alignas(64)` on buffer, head,
  and tail. `peer_counters_t` (in `stats_collector`) uses `alignas(64)` so
  adjacent peers in the unordered_map never share a cache line.
- **No allocations in hot path**: the ring buffer is fixed-size (4096 slots ×
  8 bytes = 32KB). On queue full, the event is silently dropped; the next
  200ms flush catches up from the router's status snapshot.
- **All allocations in cold path**: `std::unordered_map` insert/erase,
  `dmjson::writer_t` string building — all in the collector thread outside
  the hot path.

## Why unordered_map (not vector) for peer counters

`peer_id_t` is a monotonically-increasing `uint32_t`. The daemon may run for
months with thousands of peer lifecycle events (create → destroy). A vector
indexed by `peer_id - 1` would grow without bound, wasting memory. A map
stores only active peers. The map's `find()` is on the `shared_lock` read
path (`status_rows`), not the hot path — acceptable.

## Do not block the router thread

`status_rows()` dispatches a LOW-priority query to the router thread when
called from other threads. When called from the router thread itself (e.g.,
from a signal handler), it executes inline — do NOT call `status_rows()` from
a signal handler if the query loop could be slow (it queries all peers for
latency stats and waits up to 500ms).

## WebSocket events from any thread

`httplib::WebSocket::send()` is internally mutex-protected (`write_mutex_`).
Signal handlers on any thread (router, poller, collector) may call
`subs->emit()` → `ws.send()` safely. The `event_subscription_manager_t`
protects its channel set with its own mutex, held only briefly.
