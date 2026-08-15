## Why

A blocking message handler stalls its actor's thread: while it runs, the actor drains nothing, and for data-plane actors (peers, router) MIDI stops flowing. Today a *slow-but-returning* handler is invisible — nothing measures handler duration, so a 900 ms blocking call (DNS, file I/O, a busy loop) leaves no trace. We need the actor loop to surface it.

## What Changes

- Add a per-actor `slow_message_threshold` config (default `1000ms`) to `actor_config_t`.
- The actor loop measures each message handler invocation (`on_data` and `on_control`) with `std::chrono::steady_clock`.
- When a handler exceeds the threshold, the loop logs an `ERROR` naming the actor, the lane (data/control), the message (type name, plus content where a `to_string` exists), and the duration.
- The `stop_t` message is exempt (handled before the clock starts).
- No rate limiting: a plain `ERROR` per slow message. A slow handler *is* the throughput bottleneck, so the error rate is self-bounding (~1/actor/second); persistent errors are the signal, not noise.
- Scope is message handlers only: `on_loop`, timer/fd callbacks, and a fully *wedged* handler (never returns) are out of scope.

## Capabilities

### New Capabilities
- `slow-message-detection`: per-actor slow-message threshold and ERROR emission when a message handler exceeds it.

### Modified Capabilities

None.

## Impact

- `src/actor.hpp` — `actor_config_t::slow_message_threshold`, timing in `handle_data_safe`/`handle_control_safe`, the slow-check helper.
- `src/mailbox.hpp` — `describe_message` / `describe_control` helpers (next to `demangle_type`; reuse the existing `to_string`-else-demangle pattern).
- `src/data_message.hpp` — `to_string(data_message_t)` so data-lane messages render as `midi_to_wire{to=7, from=3}`.
- `tests/test_actor.cpp` — deterministic tests via pump mode and a `0ms` threshold.
- All 14 actors inherit the behavior with zero per-actor changes. Hot-path cost is two `steady_clock::now()` calls (~20–30 ns each) per message — negligible.
