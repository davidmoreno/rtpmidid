# MIDI Routing Specification

## Purpose

MIDI topology and flow: the router as single-writer authority, the hub data path, router-owned peer spawning, stop/remove choreography with wedged-peer escalation, requester-driven status gathers, topology event subscriptions, and ordered shutdown.

## Requirements

### Requirement: Router is the single-writer topology authority
The connection graph (peer registry and per-peer `send_to` lists) SHALL be mutated only by the router thread. Peers SHALL hold no routing state other than their router mailbox handle. Topology queries by other actors SHALL be answered via messages from the router.

#### Scenario: Connection change in one place
- **WHEN** a connect or disconnect is requested
- **THEN** only the router's map is mutated; no peer updates local routing state

### Requirement: Hub data path
Peers SHALL send received MIDI to the router as `midi_received{from, payload}`; the router SHALL look up the sender's `send_to` list, update packet counters single-threaded (no atomics), and forward `midi_to_wire{to, from, payload}` to each destination's data lane using N−1 copies plus 1 move. MIDI from an unknown sender id SHALL be dropped with a warning.

#### Scenario: Note forwarded through the hub
- **WHEN** peer A receives MIDI and is connected to peers B and C
- **THEN** the router receives one `midi_received` and posts one `midi_to_wire` to B's lane and one to C's lane

#### Scenario: Unknown sender dropped
- **WHEN** `midi_received` arrives with an id not in the registry
- **THEN** the message is dropped and a warning is logged

### Requirement: Router handlers are commit-only
Router message handlers SHALL perform only O(1) map mutations and mailbox pushes. All fallible or blocking preparation (DNS, sockets, binds, accepts, parsing, object construction) SHALL happen in the calling actor before posting to the router. The router SHALL drain its data lane before processing control messages (data-first policy).

#### Scenario: Prepared bundle committed
- **WHEN** the router receives an `add_peer` request
- **THEN** it only inserts prepared data into its registry and posts notifications, performing no I/O

### Requirement: Router-owned peer spawning
Standalone peer threads SHALL be spawned and joined by the router. A spawning request (`spawn_peer`) SHALL carry a prepared move-only bundle; the router SHALL construct the actor, spawn its thread, register its ids, post `registered{ids}` to the peer, and reply the assigned ids to the caller. A spawned peer SHALL NOT process wire traffic until it receives `registered`. If preparation fails in the caller, nothing SHALL be spawned.

#### Scenario: Spawn handshake
- **WHEN** a caller posts a prepared `spawn_peer` request
- **THEN** the router spawns the peer, the peer waits for `registered` before handling wire traffic, and the caller receives the assigned ids

#### Scenario: Failed preparation spawns nothing
- **WHEN** the caller's preparation (socket, DNS, parse) fails
- **THEN** no `spawn_peer` is posted and no thread exists

### Requirement: Hosted peer id registration
Peer ids hosted inside an existing actor (e.g. ALSA ports inside the ALSA actor) SHALL be registered via `register_peer` carrying the existing mailbox handle; removal of a hosted id SHALL only unregister it without affecting the hosting actor.

#### Scenario: ALSA port registration
- **WHEN** the ALSA actor announces a new seq port
- **THEN** the router assigns a peer id mapped to the ALSA actor's mailbox

### Requirement: Stop and remove choreography
On `remove_peer`, the router SHALL cut the peer's topology immediately, notify former connection partners with peer events, post `stop` to the peer, await `stopped` under a deadline, join the peer thread (which has already exited when `stopped` arrives), and acknowledge the requester. A peer terminating itself (e.g. remote closed the session) SHALL post `stopped` and exit; the router SHALL handle it through the same path.

#### Scenario: Remove completes
- **WHEN** `remove_peer` is requested for a connected peer
- **THEN** topology is cut before the stop, partners are notified, and the requester receives an ack after the thread is joined

#### Scenario: Self-termination
- **WHEN** a peer detects its session was closed remotely
- **THEN** it posts `stopped` and exits, and the router cleans up the same way as for a remove

### Requirement: Wedged-peer escalation without blocking the router
If `stopped` does not arrive before the deadline, the router SHALL raise the peer's jthread stop-token, grant a short window, and if still unresolved move the jthread into a `reap_actor` message to the main supervisor, reply to the requester with a warning, and forget the peer. The router SHALL NOT block joining a thread inside its own loop.

#### Scenario: Wedged peer reaped
- **WHEN** a peer misses its stop deadline and its stop-token grace window
- **THEN** the router delegates the jthread to the supervisor via `reap_actor` and continues processing without blocking

### Requirement: Fatal peer error implies removal
`actor_died` for a registered peer SHALL be treated as an implicit remove: topology cleanup and deregistration without a stop phase, with the thread joined when it has exited.

#### Scenario: Dead peer removed
- **WHEN** a peer's wrapper posts `actor_died`
- **THEN** the router removes the peer from the graph without sending it a stop

### Requirement: Requester-driven status gather
On a status request, the router SHALL answer the requester with a `status_head` (router-side statistics and the expected peer set) and SHALL scatter a status request to each peer carrying the requester's `reply_to` mailbox handle; each peer SHALL answer the requester directly. The router SHALL keep no gather state. The requester SHALL gather responses under its own deadline, treating a peer event for an expected peer as that peer being unreachable (the set shrinks) and producing a partial result on deadline. Responses with unknown correlation ids SHALL be silently discarded.

#### Scenario: Gather completes
- **WHEN** every peer answers the requester within the deadline
- **THEN** the requester merges the `status_head` with all peer responses into the full status

#### Scenario: Partial result on timeout
- **WHEN** a peer does not answer before the deadline
- **THEN** the requester produces a result marking that peer unresponsive

#### Scenario: Peer dies mid-gather
- **WHEN** a peer in the expected set terminates while the gather is in flight
- **THEN** the requester's expected set shrinks via the peer event and the gather continues without that peer

#### Scenario: Requester disappeared
- **WHEN** the requester disconnects mid-gather and responses arrive later
- **THEN** the responses are discarded without error

### Requirement: Topology event subscription
The router SHALL accept `subscribe_events{reply_to}` requests and thereafter push a `peer_event` to the subscriber's mailbox for each topology change (peer registered, removed, stopped, died). The subscription SHALL end on unsubscribe or when the subscriber's mailbox is gone. Subscribers SHALL be free to process or ignore these events, including draining them with a catch-all wait.

#### Scenario: Subscriber receives peer events
- **WHEN** a subscriber is registered and a peer connects or disconnects
- **THEN** the subscriber's mailbox receives the corresponding peer events in order

#### Scenario: Events ignored harmlessly
- **WHEN** a subscriber drains peer events with a catch-all wait
- **THEN** the events are consumed without affecting the subscriber's other processing

### Requirement: Command relay
Peer commands from the control socket SHALL be relayed through the router to the target peer's control lane. The peer SHALL reply the typed result directly to the requester's `reply_to` mailbox handle — the router relays the request only — using the correlation-id envelope.

#### Scenario: Command round trip
- **WHEN** a control client issues a peer command
- **THEN** the peer executes it on its own thread and the serialized result is posted to the requester's `reply_to` and reaches the client

### Requirement: Ordered shutdown
On a shutdown request, the router SHALL stop all peers with a bounded per-peer wait, join their threads, and acknowledge; the main supervisor SHALL stop the remaining actors and exit. Threads that do not stop in time SHALL be delegated to the supervisor's dedicated background reaper thread, which joins them off-loop (never inside a message loop); at daemon exit, any reaped thread still alive SHALL be detached with a warning and the daemon SHALL exit — shutdown SHALL NOT hang on a wedged thread.

#### Scenario: System shutdown
- **WHEN** the supervisor receives SIGTERM via signalfd
- **THEN** the control socket stops accepting commands, the router stops all peers, all threads are joined or delegated to the reaper thread, still-alive reaped threads are detached at exit, and the daemon exits
