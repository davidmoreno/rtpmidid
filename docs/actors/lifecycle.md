# Actor lifecycle and the loop

An actor is `actor_t<DataT, ControlT>` (header-only, `src/actor.hpp`), with a
type-erased base `actor_base_t` for heterogeneous actor lists (supervisor
managed actors, spawned peers of any family).

## Configuration

```cpp
struct actor_config_t {
  std::string name;
  scheduling_class_t scheduling = scheduling_class_t::normal; // normal | elevated | idle
  bool rt_enabled = false;   // SCHED_FIFO promotion for elevated actors
  int rt_priority = 10;
  actor_id_t id = 0;
  mailbox_handle_t supervisor_mailbox; // where stopped/actor_died are posted
};
```

- `supervisor_mailbox` is the **owning** actor: the router for peers, the main
  supervisor for top-level actors.
- The actor creates its own mailbox unless you pre-create one and call
  `set_mailbox(...)` before `start()` (the network listener does this so it
  can route datagrams to an accepted peer before the router spawns it).

## The loop

The core is one pass, `run_once(timeout)` — production threads call it forever,
tests drive it deterministically in pump mode:

```
run_once(timeout):
  if first pass: register the mailbox doorbell fd in the actor's poller
  mailbox.prepare()                    # read (reset) the doorbell, clear the flag
  drain lanes per the actor's policy   # data-first default, FIFO for the worker
  on_loop()                            # per-pass hook (deadline checks etc.)
  if stopping_ or stop token raised: return false
  if a selective waiter is parked and past its deadline: resume(timeout)
  if !mailbox.idle() and no waiter parked: return true   # re-drain, don't sleep
  poller.wait(min(waiter deadline, timers, timeout))     # no-lost-wakeup re-check
  return !stopping_
```

The thread wrapper (`start()` → `thread_main`) does: apply scheduling →
`on_start()` (failures are fatal) → loop while not stopping → `finish()`.
`finish()` runs `on_stop()`, closes the poller and posts `stopped` (or
`actor_died` on fatal) to the supervisor mailbox.

### Hooks

| Hook | When | Notes |
|---|---|---|
| `on_start()` | once, before the loop (thread or first pump) | failures are fatal (`actor_died`) |
| `on_data(DataT&&)` | per data-lane message | wrapped in try/catch (isolation) |
| `on_control(ControlT&&)` | per control-lane message | `stop_t` is handled by the wrapper first |
| `on_loop()` | once per loop pass after draining | for busy actors: pending-table deadline checks |
| `on_stop()` | when the loop exits | run stop hooks; the wrapper posts `stopped` |

## Lifecycle

- **`start()`** — spawns the thread with the configured scheduling class.
- **`request_stop()`** — graceful: posts `stop_t`; the loop processes it, runs
  `on_stop()` and exits (owner can join as soon as `stopped` arrives).
- **`request_stop_token()`** — escalation: raises the stop token, checked once
  per iteration (and before sleeping), so a wedged loop exits within one
  iteration.
- **`finish()`** — finalize (idempotent); also called by `pump()` when the loop
  exits, so pump-mode tests get the same `stopped` posting as threaded actors.
- **`take_thread()`** — moves the `std::jthread` out for **delegated reap**
  (router → supervisor `reap_actor_t`); the supervisor's dedicated reaper
  thread joins it off-loop.
- **`pump()`** — one `run_once(0)` pass without a thread; `false` when the
  loop exited.

### Rules

- **R1**: never block joining a thread inside your own message loop. Join only
  after `stopped` is known (the thread has exited), or delegate to the reaper.
- **R2**: every exit path posts `stopped` or `actor_died` — the wrapper
  guarantees it — so joins cannot hang on well-behaved actors.

## Drain policies

`drain_policy_t::data_first` (default): drain the data lane completely, then
exactly one control message, repeat until the control lane is empty. Neither
lane starves; a control burst delays data by at most one message per pass.
`drain_policy_t::fifo` (the worker): control lane first, then data.
Policies are pluggable via `set_drain_policy()`.

## Selective wait: `wait_for` (Erlang-style receive)

```cpp
void wait_for(
    std::move_only_function<bool(const ControlT &)> predicate,
    std::chrono::milliseconds deadline,
    std::move_only_function<void(std::optional<ControlT>)> resume);
```

- **First match wins, rest preserved**: the control lane is scanned in order;
  exactly the first matching message is consumed; non-matching messages stay
  queued in their original order.
- **Parked waits are selective, not total**: while parked, the data lane keeps
  being drained fully (MIDI unaffected), fd/timer events keep being serviced,
  and only control dispatch is gated.
- **One waiter per actor**, and **every wait is deadline-bounded**: on timeout
  the waiter resumes with `std::nullopt` and the queue contents are preserved.
  Late matching messages arriving afterwards are consumed normally (or
  discarded as unknown-corr).
- **Catch-all**: a trivially-true predicate (or plain drain) consumes
  "everything queued now, in order" — a subscriber draining and ignoring
  `peer_event`s.

Sequential request/response code is straight-line:

```cpp
router->mailbox()->post_control(status_req_t{hdr_t{corr}, mailbox_handle()});
wait_for(
    [corr](const connection_control_t &m) {
      return std::holds_alternative<status_head_t>(m) &&
             std::get<status_head_t>(m).hdr.corr == corr;
    },
    request_deadline,
    [this](std::optional<connection_control_t> res) { handle(res); });
```

The router and other always-busy actors never park; they use a
`pending_request_table_t` keyed by correlation id instead.

## Actor-local fds and timers

```cpp
add_fd_in(fd, handler);        // level-triggered epoll in the actor's own poller
add_fd_out(fd, handler);
add_fd_inout(fd, handler);
add_timer(std::chrono::milliseconds, handler);  // one-shot; re-arm in the handler
poller();                      // the actor's own poller (e.g. to host aseq/mdns libs)
```

There is no central timer authority; every fd and timer belongs to exactly one
actor's poller.

## Scheduling

`scheduling_class_t`:

- **normal** — control socket connections, mdns, listeners.
- **elevated** — data-plane actors (peers, router): with `rt_enabled` their
  thread is promoted to `SCHED_FIFO` at `rt_priority` via
  `pthread_setschedparam` at thread start; if promotion fails (no
  `RLIMIT_RTPRIO`/`CAP_SYS_NICE`) the actor falls back to nice-based elevation
  with a warning. The unit file grants `LimitRTPRIO=`.
- **idle** — the worker: `SCHED_IDLE` (fallback `nice(19)`).

Queue mutexes use `PTHREAD_PRIO_INHERIT` so elevated producers cannot be
inverted by a lower-priority queue holder.

## Supervision and shutdown

- Top-level actors (router, ALSA, mdns, worker, listeners, control socket)
  have the **supervisor's mailbox** as `supervisor_mailbox`; peers have the
  **router's mailbox**.
- The supervisor collects `stopped`/`actor_died`/`reap_actor` and owns the
  **reaper thread** that joins delegated `std::jthread`s off-loop. At daemon
  exit, still-alive reaped threads are detached with a warning — shutdown
  never hangs on a wedged thread.
- **Ordered shutdown**: control socket stops first (no new commands), then the
  router (`stop_all` → bounded per-peer stop with deadlines → reap fallback),
  then the managed actors, with per-actor deadlines and reap fallback.
- SIGTERM/SIGINT arrive via a process-wide handler writing to an eventfd the
  supervisor polls (the avahi/ALSA libraries reset the signal mask, so
  signalfd's mask-based wakeup is unreliable).
