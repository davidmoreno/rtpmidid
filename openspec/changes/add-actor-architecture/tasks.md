# Tasks: add-actor-architecture

## 1. Queue and mailbox infrastructure (spec: mpsc-queue, actor-runtime)

- [x] 1.1 Define `waker_t` and `eventfd_waker_t` (eventfd + coalescing flag) in `src/waker.hpp`: `wake()` producer-side, `prepare()` consumer-side, `fd()` exposed as the doorbell
- [x] 1.2 Define `queue_t<T>` interface (push/try_pop/capacity/drops/empty), `drop_policy_t` (`drop_incoming` / `drop_oldest`), and `mpsc_queue_t<T>` (`PTHREAD_PRIO_INHERIT` mutex, construction-time policy, atomic drop counters, wake-on-enqueue after unlock) in `src/queue.hpp`; declare future `spsc_queue_t<T>` interface reservation
- [x] 1.3 Implement `mailbox_t` in `src/mailbox.hpp`: joins data + control lanes under one shared `eventfd_waker_t`; producer `post_data`/`post_control`, consumer `prepare`/`pop_data`/`pop_control`/`idle`/`drain_data_first`
- [x] 1.4 Unit tests: per-producer FIFO ordering, full-queue drop for both policies, drop counters, non-blocking no-alloc push, coalesced burst wake (single eventfd write while consumer sleeps), no-lost-wakeup across prepare/recheck races, mailbox drain interleaving
- [x] 1.5 Bump the build to require C++23: CMakeLists `CPP_VERSION` default + drop the C++17/20 auto-detection, `compile_flags.txt`; document the toolchain floor (GCC ≥ 12.1 / Clang ≥ 16). (CMakeLists/compile_flags bump already staged; remaining: verify + document the floor.)

## 2. Actor runtime (spec: actor-runtime)

- [x] 2.1 Implement `actor_t` skeleton: owned jthread, per-actor `poller_t` instance, fd/timer registration helpers, `run_once(timeout)` core loop
- [x] 2.2 Wire the mailbox (1.3) into `actor_t`: register the doorbell fd in the actor poller, drive prepare → drain → re-check-idle loop (no-lost-wakeup protocol)
- [x] 2.3 Implement drain policies: default data-first (drain data, one control, repeat) and FIFO; make policy pluggable
- [x] 2.4 Implement lifecycle: start with scheduling class, stop control message, stop-token check per iteration, join-after-stopped
- [x] 2.5 Implement exception wrapper: per-message try/catch with log+count, fatal path posting `actor_died{id, reason}` to the supervisor mailbox
- [x] 2.6 Implement scheduling priority: pthread_setschedparam promotion for RT mode, nice fallback with warning, idle class for worker, normal class for control socket and mdns, PRIO_INHERIT on queue mutexes
- [x] 2.7 Implement threadless pump mode (no-thread construction, `pump()` driving `run_once`)
- [x] 2.8 Unit tests in pump mode: doorbell no-lost-wakeup, drain interleaving (data-first, no starvation either direction), per-message exception isolation, stop handshake, stop-token escalation
- [x] 2.9 Implement selective control wait: single parked-waiter slot (predicate + deadline), control-lane scan consuming first match, catch-all drain; data lane + fd/timer events serviced while parked
- [x] 2.10 Pump-mode unit tests: first-match ordering, non-matching messages preserved in order, timeout resume, catch-all drain, MIDI flowing during a parked wait

## 3. Message protocol (spec: actor-message-protocol)

- [x] 3.1 Define message payload types: inline MIDI payload (≥1536B, size-checked), bounded heap escape pool for oversized payloads, `midi_received`, `midi_to_wire` with `to`/`from` peer ids
- [x] 3.2 Define control-plane message variants and the `hdr{corr}` envelope; mailbox handle type (`shared_ptr`) for control-plane use
- [x] 3.3 Define requester-side deadline helper (actor-local timer; backs both the selective waiter slot of 2.9 and the router's pending table keyed by corr; late/unknown corr discarded)
- [x] 3.4 Unit tests: payload self-ownership across copies/moves, oversized payload heap-escape + pool-exhaustion drop+log+counter, lane assignment, late-response discard

## 4. Router actor (spec: midi-routing)

- [x] 4.1 Convert `midirouter_t` into an actor: registry map + `send_to` owned by the router thread, data lane (`midi_received`) and control lane
- [x] 4.2 Implement hot-path forwarding: lookup, single-threaded counters, fan-out with N−1 copies + 1 move; unknown-sender drop with warning
- [x] 4.3 Implement `spawn_peer`: construct actor from prepared bundle, spawn thread, register ids, post `registered{ids}`, reply to caller
- [x] 4.4 Implement `register_peer`/unregister for hosted ids (mailbox-handle mapping, multiple ids per mailbox)
- [x] 4.5 Implement connect/disconnect handlers with `peer_event` notifications to partners
- [x] 4.6 Implement remove/stop choreography: immediate topology cut, `stop`, `stopped` wait with deadline, join, ack; merge peer self-termination into the same path
- [x] 4.7 Implement escalation: stop-token raise, grace window, `reap_actor{jthread, reason}` to supervisor, warning ack
- [x] 4.8 Implement `actor_died` handling as implicit remove
- [x] 4.9 Implement requester-driven status gather: router answers `status_head` + scatters with `reply_to`, peers answer the requester directly, requester gathers under deadline (peer events shrink the set)
- [x] 4.10 Implement command relay and shutdown (bounded stop-all, join-all, ack)
- [x] 4.11 Implement `subscribe_events` and the `peer_event` fan-out to subscribers, with unsubscribe and mailbox-gone cleanup
- [x] 4.12 Pump-mode unit tests: forwarding fan-out, spawn/register handshake ordering, remove choreography, escalation path, requester-driven gather completion/timeout/peer-death/disconnect cases, subscription stream + catch-all drain

## 5. Peer actors

- [x] 5.1 Define the peer actor base on `actor_t`: data lane handler (encode + send), `registered` gate for wire traffic (selective wait for `registered{ids}` under the system control deadline, self-terminate on timeout: post `stopped` and exit), stop hooks
- [ ] 5.2 Port the network rtpmidi peer (connection) to the peer actor: socket in own poller, recv → `midi_received`, `midi_to_wire` → rtp encode + sendto, keepalive/reconnect timers actor-local
- [ ] 5.3 Port the network listeners to prep-and-post actors: accept/control sockets in own poller, prepare bundles, post `spawn_peer`, forget; DNS resolution delegated to the worker (`dns_resolved` reply)
- [ ] 5.4 Implement the ALSA actor: seq fd ownership, event demux by source port → `midi_received`, `midi_to_wire.to` → seq output with EAGAIN/POLLOUT retry, port announce → `register_peer`/remove
- [x] 5.5 Port the rawmidi peer to the peer actor (including the oversized-payload heap escape path)
- [x] 5.6 Move peer status/command handling to message handlers producing the existing typed variants/results on the peer thread
- [ ] 5.7 Pump-mode unit tests per peer family: recv→message, message→send, registered gating, EAGAIN retry, self-termination posting `stopped`
- [ ] 5.8 Run existing integration/regression tests against the actor-based peers; fix wire-level discrepancies

## 6. Worker and supervisor

- [x] 6.1 Implement the worker actor: FIFO control lane of function jobs, idle scheduling class, `stop` support
- [x] 6.2 Move blocking work (DNS resolution) into the worker; closures post typed results back to requester mailboxes
- [ ] 6.3 Implement the dedicated mdns actor: owns the avahi fds in its own poller at normal priority; serves status and announcement mutations (e.g. `mdns.remove`) via mailbox request/response to the control socket
- [ ] 6.4 Implement the main supervisor actor: signalfd for SIGTERM/SIGINT (replacing raw handlers), `actor_died` collection, reap list joined on a dedicated background reaper thread (off-loop); at daemon exit, detach still-alive reaped threads with a warning
- [ ] 6.5 Implement ordered shutdown: control → router (stop-all) → ALSA/worker/mdns, join with deadlines, reap fallback
- [ ] 6.6 Unit tests: job execution and result routing, mdns request/response, shutdown ordering, reap path for a wedged thread

## 7. Control socket async dispatch (spec: control-socket-jsondm delta)

- [ ] 7.1 Implement the control listener actor: owns the listening socket, accepts clients, spawns one connection actor per client (normal priority) owning the client fd + mailbox
- [ ] 7.2 Implement per-connection actors: read the client fd, parse commands, post typed requests (status/connect/disconnect/add/remove/peer commands) with `reply_to`, wait selectively with deadline, write responses byte-compatibly; optional `subscribe_events` for clients
- [ ] 7.3 Implement deadline error responses (unresponsive router/peer) and clean stop on client disconnect (abandoned waits, late responses discarded)
- [ ] 7.4 Re-run byte-compatibility tests against captured legacy control payloads; add per-connection isolation tests (stalled client does not block others)

## 8. Cutover and packaging

- [ ] 8.1 Remove the global poller singleton and the old main loop; main becomes the supervisor actor only
- [ ] 8.2 Verify no cross-thread direct calls remain (audit: only mailbox posts between actors)
- [ ] 8.3 Update `debian/rtpmidid.service`: drop process-wide `CPUSchedulingPolicy=fifo`, keep `Group=audio`, add `LimitRTPRIO=` (or `AmbientCapabilities=CAP_SYS_NICE`) so in-daemon RT promotion can succeed, and document the requirement
- [ ] 8.4 Add configuration options: RT mode enable/priority values; document fallback behavior and the central lane-capacity constants (tuned in 8.5)
- [ ] 8.5 Latency/isolation smoke measurements (hot path allocation check, note-to-note forwarding latency, behavior under a flooded peer) and record results
- [ ] 8.6 Full test suite green; update README/docs for the architecture and configuration
- [ ] 8.7 Verify Debian build-deps and CI images support C++23 (pin `gcc >= 12:1` in debian/control; ubuntu-latest and trixie are already fine) and document the toolchain floor
