# The rtpmidid Actor Architecture

rtpmidid runs as a set of **actors**. Each actor is one `std::jthread` that owns
exactly one private epoll poller and one mailbox. The **only** way to
communicate with an actor from another thread is to post a message to its
mailbox. There is no shared mutable state, no locking between actors, and no
cross-thread direct calls — ownership rules replace synchronization.

This page is the entry point. For the full picture read the sibling pages in
this directory:

| Page | What it covers |
|---|---|
| [mailbox.md](mailbox.md) | The queue layer: wake source, bounded queues, drop policies, the two-lane mailbox, the doorbell no-lost-wakeup protocol |
| [messages.md](messages.md) | The message protocol: the data plane, per-actor control variants, type-erased posting, request/response with correlation ids and deadlines |
| [lifecycle.md](lifecycle.md) | The actor loop, lifecycle, drain policies, selective wait, scheduling, supervision and shutdown |
| [creating-actors.md](creating-actors.md) | **How to create a new actor**: step by step, with a complete worked example |
| [testing.md](testing.md) | How actors are tested: deterministic threadless pump mode, the test harness, patterns |

## The model in one picture

```
        thread A (actor)                    thread B (actor)
   ┌──────────────────────┐            ┌──────────────────────┐
   │ actor_t<DataA, CtrlA>│            │ actor_t<DataB, CtrlB>│
   │  ┌────────────────┐  │            │  ┌────────────────┐  │
   │  │ own poller_t   │  │            │  │ own poller_t   │  │
   │  │ (epoll)        │  │            │  │ (epoll)        │  │
   │  └────────────────┘  │            │  └────────────────┘  │
   │  ┌────────────────┐  │            │  ┌────────────────┐  │
   │  │ own mailbox_t  │◄─┼────────────┼──│   mailbox_t    │  │
   │  │  data lane     │  │   posts    │  │  data lane     │  │
   │  │  control lane  │  │            │  │  control lane  │  │
   │  └────────────────┘  │            │  └────────────────┘  │
   └──────────────────────┘            └──────────────────────┘
```

## Core rules

1. **One owner per piece of state.** The connection graph lives only in the
   router thread; a peer's sockets only in its own poller; the ALSA seq fd and
   avahi fds only in their actors' pollers.
2. **Communication is mailbox posts only.** Fd registration, timers and
   handler invocation happen on the owning actor's thread.
3. **Messages are owned.** Crossing a queue is a copy or a move; messages never
   borrow. Data-plane MIDI payloads are inline (no allocation on the hot path).
4. **Bounded lanes with drop policies.** Queues never block and never grow
   unboundedly; floods drop per policy and are counted and logged (see
   [mailbox.md](mailbox.md) and the observability notes).
5. **Deadlines are requester-side.** Every request carries a correlation id and
   a requester deadline; responders never know about deadlines.
6. **`R1`: an actor never blocks joining a thread inside its own loop.** Joins
   happen only after `stopped` is known, or are delegated to the supervisor's
   reaper thread.
7. **`R2`: every exit path posts `stopped` (or `actor_died` on fatal).** So
   owners can join without hanging.

## Source map

| File | Contents |
|---|---|
| `src/waker.hpp` | `waker_t` interface, `eventfd_waker_t` (the doorbell) |
| `src/queue.hpp` | `queue_t<T>` interface, `mpsc_queue_t<T>` (v1: PRIO_INHERIT mutex + ring), `spsc_queue_t<T>` reservation |
| `src/mailbox.hpp` | `mailbox_base_t`, `mailbox_t<DataT, ControlT>`, `control_message_box_t`, `mailbox_handle_t` members, lane-capacity constants |
| `src/actor.hpp` | `actor_config_t`, `actor_base_t`, `actor_t<DataT, ControlT>` (header-only) |
| `src/message_core.hpp` | ids, `hdr_t`, `is_alternative`, `mailbox_handle_t`, lifecycle messages, generic replies, `make_*` helpers |
| `src/data_message.hpp` | `midi_payload_t` (inline + escape pool), `data_message_t` (the two data-plane kinds) |
| `src/<subsystem>_messages.hpp` | per-subsystem control messages + the subsystem's `*_control_t` variant + mailbox alias |
| `src/router_actor.{hpp,cpp}` | the router actor (graph authority, MIDI hub, peer lifecycle) |
| `src/peer_actor.{hpp,cpp}` | the peer actor base (registered gate, status/command handling) |
| `src/network_rtpmidi_{peer,listener}_actor.*` | RTP-MIDI connection peers and listeners |
| `src/alsa_actor.*`, `src/mdns_actor.*`, `src/worker_actor.*`, `src/supervisor_actor.*`, `src/control_socket_actor.*` | the subsystems |
| `tests/test_actor.cpp`, `tests/test_queue.cpp`, `tests/test_messages.cpp`, `tests/test_router.cpp`, `tests/test_peer.cpp`, `tests/test_network_actor.cpp`, `tests/test_alsa_bridge.cpp`, `tests/test_control_socket.cpp`, `tests/test_supervisor.cpp` | the actor tests |

The design rationale (decisions D1–D15, migration plan, rollback) lives in
`openspec/changes/add-actor-architecture/design.md`. The cutover audit and
hot-path measurements are in `../architecture.md`.

## Quick orientation

- **What is an actor?** → [lifecycle.md](lifecycle.md)
- **How do messages get from one actor to another?** → [messages.md](messages.md)
- **How do I add a new actor?** → [creating-actors.md](creating-actors.md) (start here when writing code)
- **How do I test an actor?** → [testing.md](testing.md)
