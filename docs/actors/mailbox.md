# Mailboxes and queues

The queue layer is three nested pieces:

```
eventfd_waker_t   (wake source / doorbell)      src/waker.hpp
   └─ mpsc_queue_t<T> (bounded lane)            src/queue.hpp
        └─ mailbox_t<DataT, ControlT> (the join) src/mailbox.hpp
```

## Wake source: `waker_t` / `eventfd_waker_t`

A queue signals its (possibly sleeping) consumer through a wake source:

```cpp
class waker_t {
public:
  virtual ~waker_t() = default;
  virtual void wake() noexcept = 0;    // producer side; any thread, non-blocking
  virtual void prepare() noexcept = 0; // consumer side; consume pending signal
  virtual int fd() const noexcept = 0; // the doorbell fd
};
```

`eventfd_waker_t` is the v1 implementation: one `eventfd(EFD_NONBLOCK|EFD_CLOEXEC)`
plus an atomic coalescing flag. `wake()` writes the eventfd only when the flag
was previously clear, so **a burst of enqueues costs exactly one eventfd write**
while the consumer sleeps. `prepare()` reads the counter first and then clears
the flag — this ordering is what makes the no-lost-wakeup protocol work
(below).

## Bounded queue: `queue_t<T>` / `mpsc_queue_t<T>`

```cpp
enum class drop_policy_t { drop_incoming, drop_oldest };

template <typename T> class queue_t {        // the interface (implementation-swappable)
public:
  virtual bool push(T &&item) noexcept = 0;  // never blocks, never allocates
  virtual std::optional<T> try_pop() = 0;
  virtual size_t capacity() const noexcept = 0;
  virtual uint64_t drops() const noexcept = 0;
  virtual bool empty() const noexcept = 0;
};
```

`mpsc_queue_t<T>` (v1) is a `PTHREAD_PRIO_INHERIT` mutex-protected fixed ring:

- **Capacity and drop policy are chosen at construction** — producers have no
  per-call policy logic.
- `push` never blocks and never allocates. On a full lane it applies the
  policy: `drop_incoming` drops the new element; `drop_oldest` displaces the
  oldest and stores the new one (the freshest data survives).
- **Every drop increments an atomic drop counter** (`drops()`), safe for
  concurrent producers.
- A push that enqueues calls `waker.wake()` **after releasing the lock** and
  only for enqueued elements; a dropped push never wakes.
- Per-producer FIFO ordering is preserved; no cross-producer guarantees.

`queue_t<T>` is the interface all consumers use, so the implementation can be
swapped later (SPSC ring, lock-free MPSC) without touching producers or
consumers. `spsc_queue_t<T>` is a declared interface reservation.

## The mailbox: `mailbox_t<DataT, ControlT>`

The mailbox joins N lanes under **one** wake source / doorbell fd:

```cpp
template <typename DataT, typename ControlT> class mailbox_t : public mailbox_base_t {
  // data lane:   mpsc_queue_t<DataT>
  // control lane: mpsc_queue_t<ControlT>
  // both share one eventfd_waker_t
};
```

- **The lane element types are the actor's own accepted types.** `ControlT` is
  normally the actor's control-message variant (see [messages.md](messages.md));
  `DataT` is `data_message_t` for MIDI actors or `std::monostate` for actors
  with no data lane.
- Default capacities are the central constants in `mailbox.hpp`:
  **4096** for the data lane, **256** for the control lane, both
  `drop_oldest`. These are the single place to tune lane sizes system-wide.
- **One doorbell fd** (`doorbell_fd()`) — the only fd the actor registers in
  its poller.

### Producer API (any thread)

```cpp
mailbox.post_data(data_message_t &&);   // typed: static_asserts the type is the data lane type
mailbox.post_control(SomeMessage&&);    // typed: static_asserts the type is in ControlT
```

The typed posts only compile for the actor's accepted types — posting a message
an actor does not accept is a **compile error**. Cross-actor handles (see
below) use the erased path, validated at runtime.

### Consumer API (actor thread only)

```cpp
mailbox.prepare();                        // read the doorbell, clear the flag
mailbox.pop_data();                       // optional<DataT>
mailbox.pop_control();                    // optional<ControlT>
mailbox.pop_matching(predicate);          // consume only the first match, rest preserved
mailbox.idle();                           // both lanes empty
mailbox.drain_data(handler);              // drain the data lane through a handler
mailbox.drain_data_first(on_data, on_control); // D4 default policy helper
mailbox.data_drops(); / mailbox.control_drops(); // counters for status
```

### The no-lost-wakeup protocol

The consumer loop (see [lifecycle.md](lifecycle.md) for where this lives):

```
for (;;) {
  mailbox.prepare();                  // read (reset) the doorbell, clear the flag
  drain lanes per the actor's policy;
  if (!mailbox.idle()) continue;      // a push raced the reset: re-drain, don't sleep
  poller.wait(next_deadline());       // bounded by timers / waiter deadline
}
```

A producer that posts after `prepare()` but before the drain re-check is seen
by the re-check; a producer that posts after the re-check leaves the eventfd
armed, so the next `poller.wait` returns immediately. **No wakeup is ever
lost.**

## Type-erased posting: `mailbox_handle_t` and `control_message_box_t`

Cross-actor handles (`reply_to` in requests, the supervisor mailbox, the
router's peer records) point at mailboxes whose lane types are not known at
the call site:

```cpp
class mailbox_handle_t {
  // wraps shared_ptr<mailbox_base_t>
  template <typename M> bool post_control(M &&m) const; // any control message
  bool post_data(data_message_t &&m) const;
};
```

A post through the erased handle carries the message in a small
`control_message_box_t` (type identity via `typeid` + owned storage). The
target mailbox matches it against the alternatives of **its own** control
variant:

- accepted → converted and queued;
- not accepted → **dropped and logged loudly, naming the message type** (a
  wiring bug; see the observability section).

There is deliberately **no global message catalog** — see
[messages.md](messages.md).

## Drop observability

Losses are either designed (bounded lanes under a flood) or bugs. Both are
loud:

- A full **control lane** warns rate-limited ("near-fatal") and counts.
- A full **data lane** warns rate-limited with the running drop count and
  counts.
- A message routed to an actor that does not accept it warns **every
  occurrence** and names the type.
- Drop counters are exposed in status (`status_head.router_data_drops` /
  `router_control_drops`).
