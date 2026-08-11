# Actor Runtime Specification (delta)

## ADDED Requirements

### Requirement: Actor owns its thread, poller, and mailbox
An actor SHALL own exactly one thread (`std::jthread`), one poller instance (epoll-based, reusing the existing `poller_t` class as a component), and one mailbox. There SHALL be no global poller singleton; all fd and timer registration SHALL happen in the owning actor's poller.

#### Scenario: Fd registered in owning actor
- **WHEN** an actor registers a socket fd
- **THEN** the fd is registered only in that actor's own poller instance

#### Scenario: No global poller
- **WHEN** the system is built
- **THEN** no global poller singleton exists; every poller instance belongs to exactly one actor

### Requirement: Mailbox is the only cross-thread API
The only operations permitted from other threads SHALL be posting messages to the actor's mailbox lanes. Fd registration, timer management, and handler invocation SHALL be restricted to the actor's own thread.

#### Scenario: No cross-thread calls
- **WHEN** any component needs work done by another actor
- **THEN** it posts a message to that actor's mailbox and performs no direct call into the actor's state

### Requirement: Lanes joined under one wake source
A mailbox SHALL join all of its lanes under ONE shared wake source, so a successful enqueue on any lane arms the same doorbell; the actor SHALL register exactly one doorbell fd in its poller. The v1 wake source SHALL be an eventfd guarded by an atomic coalescing flag, so a burst of enqueues costs one eventfd write while the consumer sleeps.

#### Scenario: Either lane wakes the consumer
- **WHEN** a message is enqueued on the data lane and one on the control lane
- **THEN** both arm the same doorbell and the consumer wakes once

#### Scenario: Burst coalesced into one wake
- **WHEN** N messages are enqueued while the consumer sleeps
- **THEN** the doorbell is written once and the consumer wakes once and drains all N

### Requirement: Doorbell wake protocol
On wake, the actor SHALL read (reset) the doorbell and clear the coalescing flag before draining its lanes, and SHALL re-check lane emptiness before sleeping, such that a post racing the reset is either observed by the re-check or leaves the doorbell armed — no wakeup is ever lost.

#### Scenario: No lost wakeup
- **WHEN** a producer posts a message after the consumer has read the doorbell but before it waits again
- **THEN** the eventfd holds the new count and the poller returns immediately on the next wait

#### Scenario: Race with the reset is re-checked
- **WHEN** a producer enqueues between the consumer's doorbell reset and its emptiness re-check
- **THEN** the consumer observes the element in the re-check and does not sleep (or the doorbell is armed and wakes it immediately)

### Requirement: Configurable drain policy with data priority default
The actor loop SHALL drain lanes according to a pluggable policy. The default policy SHALL drain the data lane completely (all messages queued at that moment, never waiting for more), then process exactly one control message, and repeat until the control lane is empty. The worker actor SHALL use plain FIFO. A continuous data stream SHALL NOT starve control processing, and a control burst SHALL NOT delay data processing for more than one control message. While a selective waiter is parked (see below), control dispatch follows the waiter's predicate instead of the drain policy; the data lane is drained as usual.

#### Scenario: Data-first interleaving
- **WHEN** the mailbox holds data messages and control messages
- **THEN** all currently queued data messages are processed, then one control message, then newly arrived data, then the next control message

#### Scenario: Neither lane starves
- **WHEN** the data lane receives a continuous stream while control messages are pending
- **THEN** control messages still advance one per drain pass

### Requirement: Selective control wait
The actor loop SHALL support a selective wait on the control lane: control dispatch parks with a predicate and a deadline, and resumes when the first queued control message matching the predicate arrives, consuming only that message. Non-matching messages SHALL remain queued in their original order. During a parked wait the data lane SHALL keep being drained and fd/timer events SHALL keep being serviced. At most one waiter SHALL be active per actor, and every wait SHALL be deadline-bounded.

#### Scenario: First match consumed, rest preserved
- **WHEN** a waiter's predicate matches one of several queued control messages
- **THEN** exactly the first matching message in queue order is delivered to the waiter and the others remain queued in their original order

#### Scenario: MIDI flows during a parked wait
- **WHEN** an actor is parked waiting for a control message while data messages arrive
- **THEN** the data lane is drained fully and processed as usual

#### Scenario: Timeout resumes with local outcome
- **WHEN** no matching message arrives before the waiter's deadline
- **THEN** the wait resumes with a timeout outcome and the queue contents are preserved

#### Scenario: Catch-all drains in order
- **WHEN** an actor waits with a trivially-true predicate
- **THEN** queued messages are consumed in order and processed

### Requirement: Per-message exception isolation
Message handler invocations SHALL be wrapped so that an exception thrown while handling one message is caught, logged, counted, and the actor continues processing. Exceptions that break actor invariants (fatal errors) SHALL cause the actor wrapper to post `actor_died{id, reason}` to its configured supervisor mailbox and exit the loop.

#### Scenario: Bad message does not kill the actor
- **WHEN** a handler throws while processing a corrupt message
- **THEN** the actor logs the error and continues processing subsequent messages

#### Scenario: Fatal error notifies the supervisor
- **WHEN** an actor hits a fatal error (e.g. `on_start` failure)
- **THEN** the wrapper posts `actor_died` with the reason to the supervisor mailbox and the loop exits

### Requirement: Lifecycle management
`start()` SHALL spawn the actor thread with its configured scheduling priority. Graceful stop SHALL be expressed as a control message processed by the loop; the actor SHALL run its stop hooks and exit. The loop SHALL check the jthread stop-token once per iteration as an escalation mechanism. Owners SHALL join the thread only after completion is known or via delegated reap, never by blocking inside their own message-processing loop. Delegated reap SHALL join on a dedicated supervisor background thread; `std::jthread` cannot be force-killed, so threads still alive at daemon exit SHALL be detached with a warning.

#### Scenario: Graceful stop
- **WHEN** an actor receives the stop control message
- **THEN** it runs stop hooks, closes its fds, and exits its loop so the owner can join immediately

#### Scenario: Stop-token escalation
- **WHEN** the stop-token is raised on a running actor
- **THEN** the loop observes it within one iteration and exits

### Requirement: Scheduling priority configuration
Actors SHALL be created with a scheduling class: data-plane actors (peers, router) elevated, control socket and the mdns actor normal, worker idle. When real-time mode is enabled by configuration, elevated actors SHALL be promoted via `pthread_setschedparam` at thread start; if promotion fails, the actor SHALL fall back to nice-based priority with a logged warning and the daemon SHALL remain functional. Queue mutexes in the system SHALL use `PTHREAD_PRIO_INHERIT` so elevated producers cannot be inverted by a lower-priority queue holder.

#### Scenario: RT promotion when enabled
- **WHEN** real-time mode is configured and a data-plane actor starts
- **THEN** its thread runs at the configured SCHED_FIFO priority

#### Scenario: Graceful fallback
- **WHEN** RT promotion fails (insufficient privileges)
- **THEN** the actor runs at nice-based elevated priority and a warning is logged

#### Scenario: No priority inversion
- **WHEN** an elevated data-plane producer pushes to a queue whose mutex is held by a lower-priority thread
- **THEN** the mutex's PRIO_INHERIT protocol temporarily raises the holder's priority so the producer is not blocked indefinitely

### Requirement: Threadless pump mode for tests
The actor loop SHALL be structured so that one pass (`run_once`/`pump`) can be executed without a thread. Tests SHALL be able to construct an actor, post messages, and pump deterministically, reproducing interleavings, gathers, and deadlines without sleeps or races.

#### Scenario: Deterministic test pumping
- **WHEN** a test posts messages to an actor without starting its thread and calls pump
- **THEN** the messages are processed in policy order with deterministic results

### Requirement: Actor-local timers and fds
Timers (keepalives, reconnect backoff, gather deadlines) and fd handlers SHALL be managed through the actor's own poller, without any central timer authority.

#### Scenario: Actor-local timer
- **WHEN** an actor schedules a timer
- **THEN** it fires in that actor's own loop only
