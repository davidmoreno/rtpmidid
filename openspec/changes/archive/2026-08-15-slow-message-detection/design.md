## Context

The actor runtime (`src/actor.hpp`, header-only) runs every actor as one `std::jthread` owning a poller and a mailbox. The loop is `run_once()`; messages are dispatched in `drain_with_policy()`, which routes every data message through `handle_data_safe()` → `on_data()` and every control message through `handle_control_safe()` → `on_control()`. Both wrappers already exist for per-message exception isolation, so they are the single choke point every message passes through — on both the threaded path and the threadless `pump()` test path.

A blocking handler is currently invisible: nothing measures handler duration, so a slow-but-returning handler (DNS, file I/O, a busy loop) leaves no trace while the actor drains nothing.

## Goals / Non-Goals

**Goals:**
- Detect and log a message handler that takes longer than a per-actor threshold.
- Name the actor, the lane, and the message in the log so the slow handler is immediately identifiable.
- Zero per-actor code changes; the 14 actors inherit it from the base class.
- Deterministically testable via pump mode.

**Non-Goals:**
- Detect a fully *wedged* handler (one that never returns) — the measurement is taken after the handler returns, so a deadlock produces no log. This is a diagnostic for slow-but-returns only.
- Instrument `on_loop`, timer callbacks, or fd callbacks.
- Change drain policies, scheduling, or any existing message behavior.
- Rate limit: a plain `ERROR` per slow message (see Decisions).

## Decisions

### D1 — Measure inside `handle_data_safe` / `handle_control_safe`
**Choice:** wrap the `on_data` / `on_control` call with a `steady_clock` pair, one slot in each wrapper.
**Alternatives:** timing in `drain_with_policy` (spreads the logic across both drain paths and misses the parked-waiter path) or a whole-`run_once` pass timer (can't attribute to a specific message). The wrappers already isolate every message, so they are the natural single choke point that also covers the parked selective-wait path (data is drained there too).

### D2 — Threshold is a per-actor config field
**Choice:** add `std::chrono::milliseconds slow_message_threshold{1000}` to `actor_config_t`.
**Alternatives:** a global constant (not injectable for tests, no per-actor tuning) or `settings_t`/INI (wider blast radius, overkill for a diagnostic). Per-actor matches the existing `actor_config_t` knobs and lets a test set `0ms` to force the slow path with no sleep.

### D3 — Plain `ERROR`, no rate limiting
**Choice:** log an `ERROR` on every slow message; no `ERROR_RATE_LIMIT`, no per-instance throttle.
**Rationale:** a slow handler blocks the actor thread for >1s, so a persistently slow actor emits at most ~1 error/second — the error rate is self-bounding. Persistent errors are the signal ("this actor is wedged in X"), not noise; sporadic errors are individually actionable. Rate limiting would hide exactly the repeated-slow-handler case we want to see.
**Alternatives considered:** `ERROR_RATE_LIMIT` macro (its `static` is per source line, shared across every instantiation of the same actor type — many same-type peers would suppress each other's logs) and a per-instance `last_slow_log_` throttle (correct, but adds state to solve a problem that does not exist given the self-bounding property).

### D4 — Message description reuses the `to_string`-else-demangle pattern
**Choice:** a `describe_message(M)` helper that returns `to_string(m)` when a `to_string` overload is reachable (ADL), else `demangle_type(typeid(M).name())`; a `describe_control` variant wraps `std::visit` to describe the concrete alternative, not the enclosing `std::variant`.
**Alternatives:** type-name-only logging (less actionable) or always rendering content (requires string allocation on the data hot path). The existing `control_message_box_t::make_describe` already uses this exact pattern, so it is consistent. Content is best-effort because the message is moved *into* the handler, so by the time it is known slow it is moved-from; scalars (`kind`/`from`/`to` in `data_message_t`) survive a move, payload bytes do not.

### D5 — `stop_t` is exempt
**Choice:** `handle_control_safe` handles `stop_t` before starting the clock.
**Rationale:** stop handling only sets a flag and copies an `hdr_t`; it cannot block, so timing it is noise. Keeps the threshold comparison off the shutdown path.

### D6 — A throwing handler is not slow-checked
**Choice:** the existing catch branches return immediately; the slow check runs only after a successful `on_data`/`on_control`.
**Rationale:** an exception is fast and already logs its own `ERROR`; timing the exception path adds nothing.

## Risks / Trade-offs

- **Hot-path overhead** (two `steady_clock::now()` calls per message) → VDSO `clock_gettime`, ~20–30 ns each; negligible on the MIDI data plane, so the measurement stays always-on rather than gated behind a flag.
- **Log flood from a persistently slow actor** → self-bounding (~1/actor/s); if it floods, that is the smoking gun the feature exists to surface.
- **Moved-from message content** → the type name is always intact (the variant alternative survives a move); content is best-effort, and data payloads render with `size=0`. Acceptable for a diagnostic.
- **False positives on scheduler stalls** → an elevated actor descheduled by the kernel for >1s reports a slow message even though the handler is innocent. Acceptable: it still points at a real latency problem, just possibly not in the handler itself.
- **Wedged handler silence** → a handler that never returns produces no log. Documented as a non-goal; a future watchdog (timer that fires *while* the handler runs) could cover it.

## Migration Plan

Additive and diagnostic only. The default `1000ms` threshold changes nothing until a handler actually exceeds it. No config, data, or API migration. Rollback is a revert of the touched files.

## Open Questions

- None blocking. Future: differentiate data-lane vs control-lane thresholds (MIDI is far more latency-sensitive than control), and add a watchdog for the wedged-handler case.
