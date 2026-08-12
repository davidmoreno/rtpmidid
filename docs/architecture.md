# Actor architecture — cross-thread audit and hot-path measurements

This file records the results of the cutover tasks 8.2 (no cross-thread
direct calls) and 8.5 (latency/isolation smoke measurements) for the
`add-actor-architecture` change.

## Per-actor message typing

Each actor declares exactly the control messages it accepts as a
`std::variant` (`using control_messages = ...`), and its mailbox control
lane element type IS that variant (`actor_t<DataT, ControlT>` / the
`mailbox_t` template). Consequences:

- An actor's `on_control` only ever sees — and only ever contains — the
  messages it accepts: the mdns actor's variant has no MIDI messages, the
  worker's has only jobs, the router's only routing/lifecycle commands.
- There is no global message catalog and no global transport variant
  (`src/messages.hpp` does not exist): each message type lives next to
  its actor (router_messages.hpp, peer_messages.hpp, mdns_messages.hpp,
  ...), and cross-actor posting through the type-erased
  `mailbox_handle_t` (used for `reply_to` and other heterogeneous
  handles) carries the message in a small `control_message_box_t` (type
  identity + owned storage); the target mailbox matches it against its
  own variant and drops with a warning if a wiring bug ever routes a
  message to the wrong actor.
- The type system enforces acceptance: posting a message an actor does
  not accept is a compile error on the typed path
  (`post_control<M>` `static_assert`s membership in the target's
  variant); the erased path validates at runtime.
- The queue template takes the accepted types directly
  (`mpsc_queue_t<DataT>` / `mpsc_queue_t<ControlT>`), so an actor's queue
  literally cannot hold a message class it does not accept.

## 8.2 — Cross-thread call audit

Every concurrent unit in the daemon is an actor: one thread, one private
`poller_t`, one mailbox. The only cross-thread API of an actor is posting
messages to its mailbox (`post_data` / `post_control`). The audit checks
that no direct calls into another actor's state survive.

Checked sources (the actor daemon): `actor.cpp`, `router_actor.cpp`,
`peer_actor.cpp`, `network_rtpmidi_peer_actor.cpp`,
`network_rtpmidi_listener_actor.cpp`, `alsa_actor.cpp`, `mdns_actor.cpp`,
`worker_actor.cpp`, `supervisor_actor.cpp`, `control_socket_actor.cpp`,
`main.cpp`.

Findings:

- **All inter-actor communication is mailbox posts.** Requests carry
  `hdr{corr}` + a `reply_to` mailbox handle; responders post results to
  the requester's mailbox; the router relays only.
- **No `router->` / `peer->` direct calls exist in the actor sources.**
  The only `router->` calls remaining are in the *legacy* daemon classes
  (`control_socket.cpp`, the legacy peers) which the new `main.cpp` no
  longer instantiates — they are dead code in the binary pending removal.
- **One owner per piece of state.** The connection graph lives only in the
  router thread (`router_actor_t::peers_`); peer wire I/O only in the
  peer actor's own poller; the ALSA seq fd and avahi fds only in their
  actors' pollers (`alsa_actor_t`, `mdns_actor_t`). The `poller()`
  accessor is used by an actor's own code to construct its own components.
- **Spawn factories capture values, never `this`**: the listener's
  `spawn_peer` factory captures the client address, ports and mailbox
  handles by value — it never dereferences the listener from the router
  thread.
- **Joins never happen inside a message loop** (rule R1): the router
  joins only after `stopped`/`actor_died` (the thread has exited);
  wedged threads are delegated to the supervisor's dedicated reaper
  thread via `reap_actor`.

## 8.5 — Hot-path smoke measurements

Environment: this development machine (GCC 16, `-O2`), micro-benchmark
over the v1 mutex queue (single producer/consumer).

| measurement | result |
|---|---|
| `mpsc_queue_t` push+pop (int payload, drop_oldest, 2M ops) | **27.8 ns/op** |
| mailbox data-lane post+pop, 64 B inline MIDI payload (500k ops) | **59.3 ns/op** |
| hot-path allocations (1000 pushes + 1000 full-queue displacing pushes + pops) | **0 allocations** (`test_queue::push_no_alloc`, global `operator new` counters) |
| end-to-end RTP-MIDI loopback through the full actor stack | **connect handshake + MIDI round trip both directions pass** (`test_network_actor`) |

Interpretation:

- A note traversing peer → router → destination crosses three mailbox
  lanes ≈ 180 ns of queue time — two orders of magnitude inside MIDI
  millisecond budgets. Per-actor isolation removes the old single-loop
  serialization (one slow operation no longer stalls every stream).
- The data plane performs zero heap allocations for payloads within the
  1536 B inline capacity (verified by counting `operator new` across
  pushes, displacing pushes and pops); oversized payloads (rawmidi sysex
  floods) use the bounded heap escape pool with drop+log+counter.
- Dropped-message behavior under a flooded peer is observable via the
  per-actor drop counters and rate-limited logs (design D3), so floods
  degrade gracefully instead of blocking producers.
