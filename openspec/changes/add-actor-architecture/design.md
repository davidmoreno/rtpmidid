# Design: add-actor-architecture

## Context

rtpmidid today is a single-threaded daemon: one global epoll `poller_t` singleton drives ALSA seq events, all RTP-MIDI UDP sockets, the control unix socket, avahi/mdns, and all timers. Peers and the router interact through direct synchronous calls (`router->send_midi()` → `peer->send_midi()`), `mididata_t` is a non-owning byte view, and there is no threading or locking anywhere. `debian/rtpmidid.service` currently runs the whole process at `SCHED_FIFO` priority 10 — including JSON parsing and DNS work.

Constraints:

- MIDI is latency-sensitive (ms budgets) but message-rate-low (thousands/sec, not millions).
- The codebase must remain testable; there is an existing test suite.
- Debian packaging: kernel 6.0+ cannot be assumed on all targets.
- C++23 is required (toolchain floor: GCC ≥ 12.1 / Clang ≥ 16 for `std::expected` and `std::move_only_function`; Debian bookworm GCC 12 and trixie GCC 13/14 both satisfy it).
- Messages that cross threads must own their data (no borrowing): `mididata_t` views cannot cross a queue.

## Goals / Non-Goals

**Goals:**

- Per-peer fault and stall isolation: one slow or misbehaving peer cannot block other MIDI streams.
- A MIDI hot path with no allocations and no unbounded blocking.
- Deterministic, testable concurrency: the whole actor system testable without threads.
- One owner per thread and one writer of the connection graph.
- Keep external dependencies unchanged; keep the control wire protocol byte-compatible.

**Non-Goals:**

- Lock-free queues in this change (interface yes, implementation later).
- io_uring (explicitly deferred, see decision D8).
- Direct peer-to-peer data plane (Shape B) — kept as a future additive optimization.
- Per-connection filter actors — future work; the architecture must not preclude them.
- MIDI-level niceties on peer stop (all-notes-off) — product decision, out of scope.
- C++ coroutines — excluded from this design: hidden resumption points and frame allocation add unpredictable latency to the control path; the selective wait (D15) uses a plain function-object continuation instead.

## Decisions

### D1: Actor model — one thread per actor, communication only via mailboxes

Every concurrent unit (each network peer, the ALSA subsystem, the router, the control socket, the mdns/avahi subsystem, the worker, the main supervisor) is an actor: one `std::jthread`, one private `poller_t` instance (epoll), one mailbox. The only cross-thread API of an actor is posting a message to its mailbox. No shared mutable state exists between actors; ownership rules replace locking.

Alternatives considered:
- *Shared state + fine-grained locks*: rejected — the codebase has zero existing locking discipline; actors delete whole categories of bugs instead of adding synchronization.
- *Thread pool executing peer tasks*: rejected — no isolation guarantees, cache ping-pong, harder latency reasoning.

### D2: Router on the data path ("Shape A" hub), single-writer graph

MIDI flows peer → router → destination peers. The router thread is the only authority for the connection graph. Peers hold no routing state; a peer's only topology knowledge is its router mailbox handle.

Rationale: the graph lives in exactly one place (debuggability, no projection/cache divergence); lifecycle, statistics, future filters, and observability sit in one known point; and the router's failure modes are performance (measurable), not correctness. The alternative (Shape B: cached route tables, direct peer→peer pushes) saves one hop and one copy but requires route distribution, mailbox lifetime and staleness protocols, and makes filtering/topology reasoning distributed. Shape B remains an additive future optimization; the message design does not preclude it.

Cost accepted: one extra queue hop (low µs with v1 queues) and up to 3 copies per message (recv → router lane → destination lane; fan-out uses N−1 copies + 1 move).

Keeping the router safe as a hot central point:
- **Caller prepares, owner commits**: all fallible work (DNS, sockets, binds, accepts, parsing) happens in the calling actor; the router handler only commits O(1) map mutations and mailbox pushes. Router handlers never block, never construct peers, never do I/O.
- **Two-lane inbox** with MIDI-first drain policy (see D4) so control bursts cannot delay MIDI.
- Router thread runs at the same elevated priority class as data peers.

### D3: Mailbox = two bounded lanes + one eventfd doorbell

Each actor's mailbox contains a data lane and a control lane, each a bounded queue, plus one `eventfd` doorbell registered in the actor's own epoll. The doorbell only signals "something arrived"; lane selection is the drain policy's job. One eventfd per actor (not per lane): free coalescing, one wake per burst.

Queues are configured at construction by the actor's creator: capacity and drop policy per lane. `push()` never blocks and never allocates; on a full lane it drops per policy, increments an atomic drop counter, and the actor logs drops rate-limited. Default capacities are system-wide constants defined in one central header — the single place to tune them (resolves O-6) — initially 4096 for the data lane and 256 for the control lane, validated and adjusted with the cutover measurements (tasks 8.4–8.5). Data lane default drop policy: drop oldest (on a full lane the oldest element is displaced and the new one stored, so the freshest data survives; class semantics in D14). The control lane also defaults to `drop_oldest` so load-bearing messages (e.g. `stop`) are never dropped as the newest element; displaced older control messages surface as requester-side timeouts, which is acceptable. A full control lane is still treated as a near-fatal warning (control messages are rare and some are load-bearing). Drop counters are exposed via status.

Alternatives considered:
- *Unbounded queues*: rejected — unbounded memory under a flood, and boundedness keeps the future lock-free ring upgrade open.
- *Blocking push*: rejected — would move stalls onto the producer's hot path.
- *Per-(producer,consumer) queues*: rejected for v1 — N² queues and harder consumer round-robin; one inbox per actor matches today's interleaving semantics.

### D4: Drain policy — MIDI strict priority with control progress guarantee

On doorbell wake, the actor drains the data lane completely (what is queued now, never waiting for more), then processes exactly one control message, then repeats until the control lane is empty. This gives MIDI strict priority within bursts while guaranteeing control messages advance one per pass — neither lane can starve the other. The policy is a pluggable function on the actor; the worker uses plain FIFO, control connection actors may use control-first. While a selective waiter is parked (D15) the control-lane policy is suspended: control messages are scanned against the waiter's predicate instead of dispatched; the data lane is always drained regardless.

### D5: Queue implementation behind an interface; mutex in v1

Queue interface: `push`, `try_pop`, `drain`, capacity, drop counter. V1 implementation: mutex-protected. Per-lane producer analysis (Shape A): a peer's data lane is SPSC by construction (only the router produces `midi_to_wire`); the router's data lane is MPSC; all control lanes are MPSC. Future implementations (SPSC ring for peer data lanes, bounded lock-free MPSC elsewhere) are drop-in replacements; keeping messages fixed-size and inline-stored, and lanes bounded, keeps that door open.

### D6: Message design — move/copy-only, self-owning, two-plane allocation rule

- Every message owns its payload; crossing a queue is a copy or a move. No views, no shared pointers to payloads.
- Data-plane messages must not allocate on the hot path: MIDI payload is inline fixed storage (capacity 1536 B, covering MTU-sized RTP-MIDI packets); payloads within capacity never allocate. Overflow (>1536 B, e.g. rawmidi sysex floods) uses a bounded heap escape hatch (resolves O-2): each actor has a configured escape-pool limit for oversized payloads, and when the pool is exhausted the message is dropped with log + counter. Oversized MIDI is rare, so the escape path stays off the hot path by construction.
- Control-plane messages may allocate freely (strings, status variants, `shared_ptr` mailbox handles).
- The entire system has exactly two data-plane message types: `midi_received{from, payload}` (peer → router) and `midi_to_wire{to, from, payload}` (router → peer). The `to` field supports actors hosting multiple peer ids (ALSA ports).
- The two lanes carry distinct message types: the data lane a fixed-size `data_message_t` (the two kinds above, no variant overhead) and the control lane a small `control_message_t` variant (envelope messages, typed status/command results, worker jobs as `std::move_only_function`). Control messages never pay for inline MIDI storage, and the data lane stays trivially fixed-size for the future lock-free ring.
- Several peer ids may map to one mailbox (ALSA actor hosts N ports).

### D7: Control plane — envelope, correlation ids, requester-side deadlines

Request/response flows carry `hdr{corr: u64}`. Requests that need an answer from a specific responder carry `reply_to` (a `shared_ptr` mailbox handle, D6) when the responder cannot derive it — with requester-side gathers this is the common case (status, subscriptions, peer commands, spawn). Responders are routable by construction (the router is the hub; mailbox handles route directly, no registry entry needed). Deadlines are enforced by the requester using actor-local timers; responders never know about deadlines; late responses with unknown corr are silently discarded.

Canonical flows:
- **Status scatter-gather (requester-driven)**: the requester (e.g. a control connection actor) posts `status_req{hdr, reply_to=requester}` to the router. The router replies immediately with `status_head{hdr, router stats, expected peers}` and scatters `peer_status_req{hdr, reply_to=requester}` to each peer; each peer replies `peer_status_resp{hdr, peer_id, data}` directly to the requester's mailbox. The requester then selective-waits (D15) in a loop — one wait per expected peer for `peer_status_resp{peer_id}` OR `peer_event{stopped, peer_id}` (a peer dying mid-gather shrinks the set) — under the overall deadline; on deadline it completes with a partial result marking unresponsive peers, merges everything with `status_head` (plus mdns status from the mdns actor, if enabled), and replies to the client. Non-matching control traffic arriving during the waits stays queued and is processed after (D15). The router keeps no gather state: the state machine lives in the requester's sequential code. Pending-table dispatch remains available for requesters that cannot wait (always-busy actors like the router).
- **Subscription**: a client may observe topology changes: `subscribe_events{hdr, reply_to}`. The router thereafter pushes `peer_event` (peer registered, removed, stopped, died) to the subscriber's mailbox; unsubscribing or the mailbox going away stops the stream. Subscribers treat these as ordinary control messages — process them when relevant, or drain-and-ignore with a catch-all wait (D15).
- **Command relay**: control → router → peer → back, same envelope; the result is addressed to the requester's `reply_to`.
- **Stop/remove**: control → `remove_peer{peer_id}` → router cuts topology immediately (single writer ⇒ zero stale state), notifies former partners (`peer_event`), posts `stop` to the peer, waits `stopped` under deadline, joins the thread, acks.
- **Spawn**: caller does all fallible prep, posts `spawn_peer{hdr, reply_to, bundle}`; router constructs the actor, spawns its thread, registers ids, posts `registered{ids}` to the peer, replies the assigned ids to the caller. A spawned peer gates wire traffic on `registered`: it selective-waits with the system control deadline, and on timeout (router unresponsive) self-terminates — posts `stopped` and exits.
- **Register (hosted ids)**: `register_peer{hdr, reply_to, mailbox, meta}` assigns ids mapped to an existing mailbox (ALSA port announcements); removal just unregisters.

### D8: Epoll + eventfd now; io_uring deferred behind the reactor seam

Per-actor `poller_t` (epoll, level-triggered) + eventfd doorbell is the event mechanism. io_uring was evaluated and deferred: its wins (syscall reduction, multishot recv, provided buffers, MSG_RING doorbells) matter at IOPS regimes 2–3 orders of magnitude above MIDI loads; it does not remove the dominant latency cost (scheduler wakeup of sleeping threads) nor the userspace queue synchronization; and its full feature set requires kernel ≥6.0 (unavailable on older supported targets) plus invites seccomp/hardening incompatibilities. The per-actor poller is the seam: a future io_uring reactor can replace it without touching actor logic, queues, or messages.

`signalfd` replaces the current raw SIGTERM/SIGINT handlers in the main supervisor actor (also fixing signal-handler-calls-into-poller unsafety).

### D9: Thread ownership — the router spawns and joins peer threads

The router owns every standalone peer thread end to end (`peer_record.jthread` optional; absent for hosted ids). Spawning is infallible work, so moving it into the router does not violate caller-prepares/owner-commits — it unifies graph and lifecycle ownership in one actor. Listeners (e.g. network accept) prep bundles and post `spawn_peer`, then forget; they never track children.

Rules:
- **R1**: an actor never blocks joining a thread inside its own loop. Joins happen only after completion is known (`stopped` received ⇒ thread already exited) or are delegated.
- **R2**: every actor loop exit path goes through the actor wrapper, which guarantees `stopped` or `actor_died` was posted; therefore joins cannot hang on well-behaved actors.
- **Escalation**: if `stopped` misses its deadline, the router fires the jthread stop-token (the loop checks it once per iteration), grants a short window, then moves the jthread into a `reap_actor` message to the main supervisor (router never blocks) and replies "removed with warning". Since `std::jthread` cannot be force-killed, the supervisor owns a dedicated background **reaper thread** holding the reap list: reaped jthreads are joined there, never in a message loop. At daemon exit, any reaped thread still alive is detached with a warning and the daemon exits — shutdown never hangs on a wedged thread, and nothing leaks because the process is ending.
- Peer self-termination (remote closed the session) posts `stopped` and exits — same router code path as remove.
- Fatal errors: the wrapper posts `actor_died{id, reason}` to the supervisor (peers → router; top-level actors → main); for peers this equals an implicit remove.

### D10: Thread scheduling

- Data peers and router: elevated priority. RT (`SCHED_FIFO`) when enabled by configuration (default preserves current Debian RT behavior), promoted in-process via `pthread_setschedparam` at thread start (systemd cannot set per-thread scheduling); fallback `nice(-10)`. Requires `RLIMIT_RTPRIO`/`CAP_SYS_NICE` (granted explicitly in the unit file, e.g. `LimitRTPRIO=` or `AmbientCapabilities=CAP_SYS_NICE` — `Group=audio` alone does not raise RT limits). Priority inversion guard (resolved): queue mutexes use `PTHREAD_PRIO_INHERIT` as the primary protection; priority bands are kept narrow as defense-in-depth.
- Control listener, per-connection control actors, and mdns actor: normal priority.
- Worker: `SCHED_IDLE` — it must never take CPU from the data plane. Hosts blocking jobs (DNS resolution etc.) as `std::move_only_function` messages (C++23; no copy requirement, small closures stored inline); closures capture the requester's mailbox and corr and post typed results back (e.g. `dns_resolved`). The worker never knows result types.
- `debian/rtpmidid.service`: drop process-wide `CPUSchedulingPolicy=fifo`; RT becomes per-thread in-daemon policy.
- A busy-poll-before-sleeping hybrid (latency vs CPU knob) is a later optimization, not in this change.

### D11: ALSA actor — one actor, many ports

The single ALSA seq fd is owned by one ALSA actor. It demuxes incoming seq events by source port to peer ids (`midi_received`), writes outgoing `midi_to_wire` to the selected port (opened `SND_SEQ_NONBLOCK`; on EAGAIN arm POLLOUT and retry), and posts `register_peer`/remove on seq port announce events. Its `registered` message carries the set of ids.

### D12: Testability — threadless pump mode

The actor loop is structured as `run_once(timeout)`; production threads call it forever, tests construct actors without threads, post messages, and call `pump()` deterministically. Gathers, deadlines, and interleavings become reproducible without sleeps or races. Message handlers are per-message try/catch so a corrupt message never kills an actor.

### D13: Dedicated mdns actor

The avahi/mdns fds (announcements, remote browsing) are owned by a dedicated mdns actor — one thread, its own poller, normal scheduling priority (resolves O-3). mdns is not latency-sensitive and must neither contend with the data plane nor stall control; keeping it on its own actor also leaves the worker purely job-based (DNS, etc.) and gives mdns a clean lifecycle owner. Control connection actors request mdns status and announcement mutations (e.g. `mdns.remove`) via mailbox request/response instead of direct `control.mdns` access.

Alternatives considered: *worker-hosted avahi fds* (fewer actors but mixes mdns fd events with blocking jobs in one loop) and *main-supervisor-hosted* (contends with shutdown choreography) — both rejected.

### D14: Queue and mailbox class structure

D3/D5 realized as three layers, each a well-defined class family:

**1. Wake source (`waker_t`, v1 `eventfd_waker_t`) — the signal mechanism.** A queue signals its consumer through a wake source: `wake()` is producer-side (any thread, non-blocking, safe concurrently), `prepare()` is consumer-side (reset before draining). The v1 implementation wraps one `eventfd(EFD_NONBLOCK | EFD_CLOEXEC)` plus an atomic coalescing flag: `wake()` does `if (!signaled_.exchange(true)) eventfd_write(fd_, 1)`, so a burst of enqueues costs exactly one eventfd write while the consumer sleeps (the design's "one wake per burst"); the fd is the actor's doorbell.

**2. Queues (`queue_t<T>` interface; v1 `mpsc_queue_t<T>`; future `spsc_queue_t<T>`) — bounded containers with a bound wake source.** `queue_t` exposes `push(T&&) -> bool` (enqueued or dropped; never blocks, never allocates), `try_pop()`, `capacity()`, `drops()`, `empty()`; D5's `drain` is a free helper over `try_pop`. `mpsc_queue_t` (v1): one `PTHREAD_PRIO_INHERIT` mutex + deque, construction-time capacity and drop policy, atomic drop counter; `push` enqueues under lock, unlocks, and only then calls `waker_.wake()` — never signals while holding the lock, never signals a dropped push. `spsc_queue_t` (future; interface reserved per the non-goal): bounded ring + per-slot sequence counters, exactly one producer thread and one consumer thread, same waker binding.

**3. Mailbox (`mailbox_t`) — the join.** Owns one `eventfd_waker_t` and binds N lanes to it; v1 joins exactly two: a data lane (`mpsc_queue_t<data_message_t>`) and a control lane (`mpsc_queue_t<control_message_t>`), each constructed with the shared waker. The lanes carry distinct message types (D6): `data_message_t` is fixed-size (inline MIDI payload, two kinds, no variant overhead); `control_message_t` is a small variant that never pays for inline MIDI storage. Mailboxes are created as `shared_ptr<mailbox_t>`: the owning actor holds one reference and control-plane messages carry the others as handles (D6/D7), so posting to a mailbox whose actor has terminated remains safe — the queues live until the last handle is dropped. One doorbell fd for the actor (`doorbell_fd()`); producer API `post_data`/`post_control`; consumer API `prepare()`/`pop_data`/`pop_control`/`idle()` plus `pop_matching(predicate)` — scans the control lane and consumes only the first match while non-matching elements remain in order (the D15 selective-wait affordance) — and a `drain_data_first` helper embodying D4's default policy (the policy itself stays pluggable on the actor). The pattern generalizes to N queues by binding further queues to the same waker.

```cpp
// waker.hpp
class waker_t {
public:
  virtual ~waker_t() = default;
  virtual void wake() noexcept = 0;     // producer side; any thread, coalescing-safe
  virtual void prepare() noexcept = 0;  // consumer side; consume pending signal
};
class eventfd_waker_t final : public waker_t {
  int fd_; std::atomic<bool> signaled_{false};
  // wake(): if (!signaled_.exchange(true)) eventfd_write(fd_, 1)
  // prepare(): eventfd_read(fd_); signaled_.store(false)
};

// queue.hpp
enum class drop_policy_t { drop_incoming, drop_oldest };

template <typename T>
class queue_t {                       // interface (D5)
public:
  virtual ~queue_t() = default;
  virtual bool push(T&& item) noexcept = 0;      // true = enqueued, false = dropped
  virtual std::optional<T> try_pop() = 0;
  virtual size_t capacity() const noexcept = 0;
  virtual uint64_t drops() const noexcept = 0;
  virtual bool empty() const noexcept = 0;
};

template <typename T>
class mpsc_queue_t final : public queue_t<T> {   // v1: PRIO_INHERIT mutex + deque
  // push: lock; full -> policy (drop_incoming: return false | drop_oldest: displace front);
  //       enqueue; unlock; waker_.wake() only when enqueued, never under the lock
};

template <typename T>
class spsc_queue_t final : public queue_t<T> {}; // future: bounded ring + sequence counters

// mailbox.hpp — the join: N lanes, one wake source, one doorbell fd
// Instances live in shared_ptr<mailbox_t>: the actor holds one ref, control messages carry others
class mailbox_t {
  eventfd_waker_t waker_;                    // declared first: lanes bind to it
  mpsc_queue_t<data_message_t> data_;        // 4096, drop_oldest (freshest wins)
  mpsc_queue_t<control_message_t> control_;  // 256, drop_oldest (newest control always lands)
public:
  int doorbell_fd() const;                              // the only fd the actor registers
  bool post_data(data_message_t&&);                     // producer API (any thread)
  bool post_control(control_message_t&&);
  void prepare();                                       // consumer API (actor thread only)
  std::optional<data_message_t> pop_data();
  std::optional<control_message_t> pop_control();
  std::optional<control_message_t> pop_matching(auto&& pred); // D15: first match, rest preserved
  bool idle() const;
  void drain_data_first(auto&& on_data, auto&& on_control);   // D4 default policy
};
```

Consumer loop (the no-lost-wakeup protocol, D8):

```cpp
for (;;) {
  mailbox.prepare();                     // read eventfd, clear flag
  mailbox.drain_data_first(handle_data, handle_control);
  if (!mailbox.idle()) continue;         // a push raced the clear: re-drain, don't sleep
  poller.wait(next_timer_deadline());  // bounded by the nearest actor-local timer (D12 run_once(timeout))
}
```

Drop policies are named by what is dropped: `drop_incoming` (full → new element dropped) and `drop_oldest` (full → oldest displaced, new stored). Both lanes default to `drop_oldest`: the data lane keeps the freshest data under a flood; the control lane guarantees the newest control message lands (a displaced older one surfaces as a requester timeout), and a full control lane remains a near-fatal warning. New headers: `src/waker.hpp`, `src/queue.hpp`, `src/mailbox.hpp`.

### D15: Selective control wait (Erlang-style receive)

The actor runtime offers a selective wait on the control lane: `wait_for(predicate, deadline)` parks control dispatch until the first queued control message satisfying the predicate arrives, and returns it. Semantics are Erlang `receive`:

- **First match wins, rest preserved**: messages are scanned in queue order; exactly the first matching one is consumed; all non-matching messages stay queued, in order, for later. A waiter never consumes what it is not waiting for.
- **Parked waits are selective, not total**: while parked, the data lane is still drained fully (MIDI unaffected), fd/timer events are still serviced, and non-matching messages accumulate in order. Only control dispatch is gated.
- **One waiter per actor**: the loop holds a single waiter slot (predicate + deadline); handlers initiate a wait and resume when it matches or times out. Sequential request/response code — send request, `wait_for` the response, repeat — is straight-line code, not pending tables or continuations.
- **Deadline is mandatory**: every wait carries a requester-side deadline (actor-local timer); on timeout the wait resumes with a local timeout outcome, queue contents preserved. Late matching messages arriving afterwards are consumed normally (or discarded as unknown-corr). Critical control (e.g. `stop`) queued behind a parked wait is processed after the wait resolves — waits are bounded, so shutdown delay is bounded by the wait deadline by construction.
- **Catch-all**: a trivially-true predicate (or plain drain) consumes "everything queued now, in order" — e.g. a subscriber draining and discarding `peer_event`s.

Usage: leaf actors that can afford to park — per-connection control actors (the status gather and command waits of D7 become straight-line loops), and any actor wanting an answer before proceeding (e.g. a peer awaiting `registered{ids}` before handling wire traffic). The router and other always-busy actors never park; they keep pending-table dispatch (D7). Implementation: one parked-waiter slot in the actor loop; predicates and continuations are plain function objects — C++ coroutines are explicitly excluded (non-goal): coroutine frames allocate and resumption points are hidden, adding unpredictable latency to the control path, and the waiter slot needs neither. C++23 is required and supplies the types used here: `std::expected` for wait outcomes (message or timeout), `std::move_only_function` for continuations and worker jobs, `std::format` replacing fmt. Pump mode makes waits deterministic in tests.

Consequence for the control socket: each client connection becomes its own actor — a control listener owns the listening socket and spawns one connection actor per accepted client (client fd + mailbox, normal priority). This is what makes per-connection `wait_for` possible and gives per-client isolation (a stalled client cannot block other connections or the data plane). Alternative considered: one control socket actor with a pending table per connection (the original plan) — rejected as primary because selective waits cannot park a shared loop; it remains available if thread-per-connection proves too heavy.

## Risks / Trade-offs

- [Router is the serialization point for all MIDI] → router handlers are O(1) commit-only, two-lane MIDI-first drain, elevated priority; measured throughput headroom is orders of magnitude above realistic loads; Shape B remains an escape hatch.
- [Extra hop adds latency per note] → bounded and measurable (low µs with v1 mutex queues); MIDI budgets are ms-scale; the hop is the price of single-writer topology.
- [Mutex queues + RT priorities → priority inversion] → narrow priority bands, `PTHREAD_PRIO_INHERIT` on queue mutexes; disappears with lock-free queues later.
- [Dropping on full lanes loses MIDI under pathological floods] → drop counters + rate-limited logs make it observable; capacities tunable; drop-oldest (displace) keeps the freshest data.
- [Large rewrite touches every peer class] → migrate behind the actor interface with the existing typed status/command interfaces unchanged at the wire level; behavior is regression-tested via pump-mode unit tests plus existing integration tests.
- [RT promotion fails on misconfigured hosts] → graceful fallback to `nice`, warning logged; daemon remains functional.
- [A wedged top-level actor] → supervisor receives `actor_died`, reaper thread joins reaped jthreads off-loop; at exit, still-alive threads are detached with a warning — failures become data instead of silent hangs, and shutdown never hangs.
- [A parked selective waiter delays other control traffic] → waits are deadline-bounded by construction and restricted to leaf actors (control connections); the router never parks; non-matching messages remain queued in order and are processed after the wait — worst-case delay for critical control (e.g. `stop`) equals the wait deadline.

## Migration Plan

1. Land infrastructure without behavior change: `mpsc_queue_t`, `actor_t`, message types, envelope; fully unit-tested in pump mode.
2. Port peers one family at a time behind actors (network rtpmidi first, then ALSA, then rawmidi), keeping the control wire protocol byte-compatible at every step.
3. Convert the router to the hub actor; replace direct calls with messages; implement scatter-gather and lifecycle.
4. Convert the control socket to a listener spawning one connection actor per client (D15): requests are posted with `reply_to` and answered via selective waits under deadlines.
5. Replace main loop with the supervisor actor (signalfd, shutdown choreography, reap list); update `debian/rtpmidid.service`.
6. Remove the global poller singleton; `poller_t` remains as the per-actor component.

Rollback: each step keeps tests green and the wire protocol unchanged; reverting a step reverts to the previous working architecture.

## Open Questions

- Peer stop semantics (drain-and-send window duration; all-notes-off) — product decision deferred (explicitly kept open; the stop/remove choreography in D7/D9 does not depend on it).

Resolved (recorded in decisions): **O-2** → bounded heap escape hatch for oversized payloads (D6); **O-3** → dedicated mdns actor (D13); **O-6** → central system-wide lane-capacity constants (D3); **priority inversion guard** → `PTHREAD_PRIO_INHERIT` on queue mutexes with narrow bands as defense-in-depth (D10).
