# Proposal: add-actor-architecture

## Why

rtpmidid is a single-threaded daemon: every peer (ALSA, RTP-MIDI network, rawmidi), the router, the control socket, mdns, and all timers share one epoll loop and one thread. One slow operation (a DNS lookup, a blocked write, a control burst) stalls every MIDI stream in the system, and latency between a received and a forwarded message is bounded by whatever else the loop is doing. We want per-peer isolation, a stall-free MIDI hot path, and better inter-message latency — by restructuring the daemon as independent actor threads communicating only through bounded message queues.

## What Changes

- Introduce a generic `actor_t` runtime: each actor owns one thread, one epoll-based poller instance, and a mailbox; the global poller singleton is removed.
- Introduce bounded MPSC message queues (v1 mutex-based, behind an interface that allows later lock-free/SPSC implementations) with caller-configured drop policies and observable drop counters.
- All inter-thread communication happens exclusively via mailbox messages (move/copy-only, self-owning payloads). No shared mutable state, no cross-thread calls.
- The MIDI router becomes the single authority for the connection graph and the data path (hub shape): peers send `midi_received` to the router; the router forwards `midi_to_wire` to destinations. Peers hold no routing state.
- The router owns the full peer lifecycle: it spawns and joins standalone peer threads (`spawn_peer`), registers hosted peer ids (`register_peer`, e.g. ALSA ports), and drives stop/remove choreography with deadlines and escalation.
- Control-plane operations (status, commands, connect/disconnect, add/remove) become asynchronous request/response flows with correlation ids, `reply_to` mailbox handles, and requester-side deadlines. The actor runtime gains an Erlang-style selective wait on the control lane (`wait_for(predicate, deadline)`), making request/response code straight-line: the control socket becomes a listener spawning one connection actor per client, and a status gather is requester-driven (the router answers with stats + expected peers and scatters with `reply_to`; peers answer the requester directly).
- Thread scheduling policy: only data-plane actors (peers, router) are eligible for real-time priority (opt-in, preserving current Debian FIFO behavior); the control socket and a dedicated mdns actor run at normal priority; the worker runs at idle priority and receives blocking jobs (DNS etc.) as functions; queue mutexes use `PTHREAD_PRIO_INHERIT` to prevent inversion.
- **BREAKING (internal)**: `poller_t` singleton removed; `midirouter_t`/`midipeer_t` direct-call interfaces become message handlers; `main()` becomes a supervisor actor owning signalfd-based signal handling.
- io_uring explicitly deferred: the reactor stays epoll + eventfd behind the per-actor poller abstraction (kernel ≥6.0 floor and marginal benefit at MIDI message rates; see design.md).

## Capabilities

### New Capabilities

- `mpsc-queue`: Bounded message queues with per-queue capacity and drop policy chosen at construction, non-blocking push, drop counters, and an implementation-swappable interface (mutex v1; SPSC/lock-free later).
- `actor-runtime`: Generic actor class — owned thread, per-actor poller, two-lane mailbox with eventfd doorbell, configurable drain policy, selective control wait with mandatory deadlines (Erlang-style receive), per-message exception handling with supervisor notification, lifecycle (start/stop/join/stop-token escalation), scheduling priority configuration, and a threadless pump mode for deterministic tests.
- `actor-message-protocol`: The message catalog and envelope rules: move/copy-only self-owning payloads, inline MIDI payload storage, data vs control lane assignment, request/response correlation ids, requester-side deadlines, and drop/overflow semantics.
- `midi-routing`: The router actor as single-writer topology authority and MIDI data hub; `spawn_peer`/`register_peer` lifecycle with router-owned spawn and join; stop/remove choreography; requester-driven status gather with `reply_to`; topology event subscription (`subscribe_events` → `peer_event` stream); command relay; fatal-peer handling and reap escalation to the supervisor.

### Modified Capabilities

- `midipeer-typed-interface`: Peer status and command interactions become asynchronous message exchanges (the peer produces the same typed status variant and command results, but in response to mailbox requests on its own thread); wire shapes unchanged.
- `control-socket-jsondm`: A control listener spawns one connection actor per client; requests are posted into the actor system with `reply_to` and the connection actor waits selectively under a deadline, writing deadline-based error responses when the router or a peer is unresponsive; wire protocol unchanged.

## Impact

- **Code**: `src/main.cpp` (supervisor), `src/midirouter.{cpp,hpp}`, `src/midipeer.{cpp,hpp}`, all peer implementations (`local_alsa_*`, `network_rtpmidi_*`, `local_rawmidi_peer`), `src/control_socket.{cpp,hpp}`, `include/rtpmidid/mdns_rtpmidi.hpp` + `lib/mdns_rtpmidi.cpp` (dedicated mdns actor), `include/rtpmidid/poller.hpp` (singleton removed, class kept as per-actor component), new actor/queue/message infrastructure.
- **Dependencies**: none new (no liburing, no external queue library); pthread scheduling APIs used directly. **Toolchain**: C++23 now required (was C++17/20 auto-detect; fmt optional since `std::format` covers it); C++ coroutines are excluded from the implementation (latency, see design D15).
- **Deployment**: `debian/rtpmidid.service` RT policy moves from process-wide `CPUSchedulingPolicy=fifo` to in-daemon per-thread promotion (needs `RLIMIT_RTPRIO` or `CAP_SYS_NICE`, granted explicitly in the unit file via `LimitRTPRIO=` — `Group=audio` alone does not raise RT limits); service file updated accordingly.
- **Behavior**: control responses may report unresponsive components instead of hanging; per-actor drop counters become observable; MIDI latency characteristics change (bounded extra queue hop, improved isolation).
