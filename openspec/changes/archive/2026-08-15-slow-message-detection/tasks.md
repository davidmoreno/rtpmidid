## 1. Message description helpers

- [x] 1.1 Add `describe_message(const M&)` to `src/mailbox.hpp` (next to `demangle_type`): returns `to_string(m)` when a `to_string` overload is reachable via ADL, else `demangle_type(typeid(M).name())`
- [x] 1.2 Add `describe_control(const std::variant<Ts...>&)` to `src/mailbox.hpp`: `std::visit` to describe the concrete alternative rather than the enclosing variant
- [x] 1.3 Add `to_string(const data_message_t&)` to `src/data_message.hpp` rendering kind (`midi_received`/`midi_to_wire`) plus `from`/`to` peer ids

## 2. Actor runtime instrumentation

- [x] 2.1 Add `std::chrono::milliseconds slow_message_threshold{1000}` to `actor_config_t` in `src/actor.hpp`
- [x] 2.2 Add a `check_slow(lane, msg, t0)` helper in `src/actor.hpp`: when `steady_clock::now() - t0` exceeds the threshold, log an `ERROR` naming the actor, lane, message (via `describe_message`/`describe_control`), and duration
- [x] 2.3 In `handle_data_safe`: capture `t0`, call `on_data` (keep existing try/catch isolation), and run `check_slow` only after a successful call
- [x] 2.4 In `handle_control_safe`: handle `stop_t` before starting the clock; otherwise capture `t0`, call `on_control`, and run `check_slow` after success (return on exception, no slow-check)

## 3. Tests

- [x] 3.1 Pump-mode test in `tests/test_actor.cpp`: `0ms` threshold + one data message → slow-message ERROR emitted, asserting the actor name, data lane, and message
- [x] 3.2 Pump-mode test: `0ms` threshold + one control message → slow-message ERROR emitted, asserting the control lane and message type
- [x] 3.3 Test: handler completing within the default threshold emits no slow-message ERROR
- [x] 3.4 Test: `stop_t` at a `0ms` threshold emits no slow-message ERROR

## 4. Verification

- [x] 4.1 Build the daemon and tests; run the actor test suite
- [x] 4.2 Run the full test suite and confirm no regressions
- [x] 4.3 `openspec validate slow-message-detection` passes
