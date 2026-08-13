# MPSC Queue Specification

## Purpose

The bounded producer-to-consumer queue used for actor lanes: non-blocking push, construction-time drop policies, wake-source binding, observable drop counters, and swappable implementations.

## Requirements

### Requirement: Bounded non-blocking push
Each queue SHALL have a fixed capacity chosen at construction. `push()` SHALL never block and SHALL never allocate. `push()` SHALL return whether the element was enqueued or dropped.

#### Scenario: Push under capacity
- **WHEN** an element is pushed to a queue with free capacity
- **THEN** the element is enqueued and push reports success

#### Scenario: Push on full queue
- **WHEN** an element is pushed to a queue at capacity
- **THEN** push applies the configured drop policy, does not block, does not allocate, and reports the drop

### Requirement: Drop policy fixed at construction
The drop policy and capacity of a queue SHALL be chosen by the queue's creator at construction time, not by producers at push time. Producers SHALL contain no per-call policy logic.

#### Scenario: Policy set once
- **WHEN** a queue is constructed with a capacity and drop policy
- **THEN** every subsequent full-queue push applies that same policy without consulting the caller

### Requirement: Drop policy semantics
Two drop policies SHALL exist, named by what is dropped: `drop_incoming` (a full queue drops the newly pushed element) and `drop_oldest` (a full queue displaces its oldest element and stores the new one). The data lane default SHALL be `drop_oldest` so the freshest MIDI data survives a flood. The control lane default SHALL also be `drop_oldest` so a load-bearing message (e.g. `stop`) is never dropped as the newest element; displaced older control messages SHALL surface as requester-side timeouts. A full control lane SHALL be logged as a near-fatal warning.

#### Scenario: Incoming dropped on full queue
- **WHEN** a queue with `drop_incoming` is full and an element is pushed
- **THEN** the new element is dropped and the drop counter increments

#### Scenario: Oldest displaced on full queue
- **WHEN** a queue with `drop_oldest` is full and an element is pushed
- **THEN** the oldest element is dropped, the new element is stored, and the drop counter increments

### Requirement: Wake source bound at construction
Each queue SHALL be constructed with a reference to a wake source (`waker_t`) that outlives the queue. A push that enqueues an element SHALL notify the wake source after releasing the queue's internal locks; a push that drops an element SHALL NOT notify. The queue SHALL never notify while holding an internal lock.

#### Scenario: Enqueue wakes the consumer
- **WHEN** a push successfully enqueues an element
- **THEN** the bound wake source is notified exactly once, after locks are released

#### Scenario: Drop does not wake
- **WHEN** a push is dropped because the queue is full
- **THEN** the wake source is not notified

### Requirement: Observable drop counters
Each queue SHALL maintain a drop counter incremented on every dropped element. The counter SHALL be safe for concurrent producer updates and SHALL be readable for inclusion in actor status reporting.

#### Scenario: Counter increments on drop
- **WHEN** elements are dropped because the queue is full
- **THEN** the drop counter reflects the total number of dropped elements

### Requirement: Implementation-swappable interface
Queues SHALL be used exclusively through an interface exposing push, pop/drain, capacity, and drop counters. The first implementation SHALL be mutex-based with `PTHREAD_PRIO_INHERIT` on the mutex (see the actor-runtime scheduling requirement). The interface SHALL allow replacing the implementation (e.g. SPSC ring, bounded lock-free MPSC) without changes to producers or consumers.

#### Scenario: Mutex implementation behind the interface
- **WHEN** the system runs with the v1 implementation
- **THEN** all queue users interact only through the interface, with a mutex-based implementation underneath

#### Scenario: Swap without touching users
- **WHEN** a queue implementation is replaced by another satisfying the interface
- **THEN** no producer or consumer code changes are required

### Requirement: Per-producer FIFO ordering
A queue SHALL preserve FIFO ordering of elements pushed by a single producer. No ordering guarantees are made across different producers sharing one queue.

#### Scenario: Single-producer order preserved
- **WHEN** one producer pushes messages M1 then M2 into a queue
- **THEN** the consumer pops M1 before M2
