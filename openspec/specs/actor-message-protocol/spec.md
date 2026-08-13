# Actor Message Protocol Specification

## Purpose

Message ownership and lane rules for the actor system: self-owning payloads, an allocation-free data plane, class-based lane assignment, requester-side deadlines, and mailbox lifetime safety.

## Requirements

### Requirement: Move/copy-only self-owning messages
Every message SHALL own its payload. Crossing a queue SHALL be a copy or a move; messages SHALL NOT contain non-owning views, references, or shared pointers to payload data. Posting a message whose producer is destroyed SHALL leave the delivered payload fully valid.

#### Scenario: Payload survives producer destruction
- **WHEN** a message is posted and the producing scope is destroyed before delivery
- **THEN** the consumer receives a fully valid, self-contained payload

#### Scenario: No views cross queues
- **WHEN** message types are reviewed
- **THEN** none contains a non-owning pointer/view into another object's storage

### Requirement: Allocation-free data plane with inline MIDI payload
Data-plane messages SHALL carry MIDI payload in inline fixed storage of at least 1536 bytes (covering MTU-sized RTP-MIDI packets). Data-plane message construction, queue crossing, and handling SHALL NOT allocate heap memory for payloads within the inline capacity. Payloads exceeding the inline capacity SHALL use a bounded heap escape hatch: allocation is permitted up to a per-actor escape-pool limit; when the pool is exhausted the payload SHALL be dropped with a rate-limited log and a drop counter increment. Control-plane messages MAY allocate freely.

#### Scenario: Hot-path message without allocation
- **WHEN** a MIDI message is received, posted to the router, and forwarded to a destination
- **THEN** no heap allocation occurs in the message path

#### Scenario: Oversized payload escaped
- **WHEN** a payload larger than the inline capacity arrives
- **THEN** it is carried through the heap escape hatch up to the pool limit; when the pool is exhausted it is dropped, a counter is incremented, and a rate-limited warning is logged

### Requirement: Lane assignment by message class
Data-plane messages (`midi_received`, `midi_to_wire`) SHALL use the data lane; all other messages SHALL use the control lane. A producer SHALL use exactly one lane for a given message type, preserving per-producer FIFO ordering for that stream.

#### Scenario: MIDI on the data lane
- **WHEN** a peer posts `midi_received` or the router posts `midi_to_wire`
- **THEN** the message travels on the data lane

#### Scenario: Control messages on the control lane
- **WHEN** lifecycle, request/response, or event messages are posted
- **THEN** they travel on the control lane

### Requirement: Request/response envelope with requester-side deadlines
Control-plane requests SHALL carry a correlation id (`corr`). Deadlines SHALL be enforced exclusively by the requester using actor-local timers; responders SHALL have no knowledge of deadlines. On timeout the requester SHALL produce a local error outcome. Responses arriving with a correlation id unknown to the receiver SHALL be silently discarded.

#### Scenario: Timeout produces local error
- **WHEN** a requester's deadline expires before the response arrives
- **THEN** the requester reports an error outcome locally without any remote cancellation

#### Scenario: Late response discarded
- **WHEN** a response arrives after the requester timed out and discarded its pending entry
- **THEN** the response is silently discarded with no error

### Requirement: Minimal data-plane message surface
The system SHALL define exactly two data-plane message types: `midi_received{from, payload}` (peer to router) and `midi_to_wire{to, from, payload}` (router to peer). The `to` field SHALL allow one actor to host multiple peer ids (e.g. ALSA ports). Multiple peer ids MAY map to the same mailbox.

#### Scenario: Two data-plane message types
- **WHEN** the message catalog is inspected
- **THEN** only `midi_received` and `midi_to_wire` exist as data-plane messages

#### Scenario: Multi-id actor addressed by sub-id
- **WHEN** the router forwards MIDI to a peer id hosted by a multi-port actor
- **THEN** the `to` field selects the correct port within that actor

### Requirement: Mailbox handle lifetime safety
Mailboxes SHALL be shared between actors only as `shared_ptr` handles, and only in control-plane messages. Posting to a mailbox whose owning actor has terminated SHALL be safe: the queue object lives until the last handle is dropped, and messages into an undrained mailbox are bounded and eventually discarded with the queue.

#### Scenario: Post after actor death is safe
- **WHEN** an actor posts to the mailbox of an actor that has already terminated
- **THEN** the post succeeds harmlessly (bounded drop) and no undefined behavior occurs
