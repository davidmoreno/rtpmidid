# Testing actors

Actors are designed to be tested **without threads**: the loop is a single
`run_once(timeout)` pass, so tests construct an actor, post messages and call
`pump()` deterministically — no sleeps, no races (unless the test deliberately
exercises real threads).

## The harness

Tests use `tests/test_case.hpp`:

```cpp
#include "test_case.hpp"
#include "test_utils.hpp"   // test_control_t / test_mailbox_t (permissive test types)

void test_something() {
  // ... assertions ...
  ASSERT_TRUE(x);
  ASSERT_EQUAL(a, b);
  ASSERT_FALSE(y);
  FAIL("message");
}

int main(int argc, char **argv) {
  test_case_t testcase{ TEST(test_something), /* ... */ };
  testcase.run(argc, argv);
  return testcase.exit_code();
}
```

Register the executable in `tests/CMakeLists.txt`:

```cmake
add_executable(test_myactor test_myactor.cpp)
target_link_libraries(test_myactor -pthread rtpmidid-shared rtpmidid2-static)
target_link_libraries(test_myactor ${FMT_LIBRARIES})
add_test(NAME test_myactor COMMAND test_myactor)
```

`test_utils.hpp` provides `test_control_t` (a permissive control variant with
the message types the tests use) and `test_mailbox_t` (its mailbox) — every
test *declares* what it accepts, the same way actors do.

## Deterministic pump mode

```cpp
void test_ticker_emits_ticks() {
  auto target = std::make_shared<test_mailbox_t>();
  ticker_actor_t ticker(actor_config_t{.name = "ticker"});
  ticker.mailbox()->post_control(ticker_set_t{hdr_t{7}, 5ms, target});

  // pump() = one loop pass (run_once(0)); timers fire during the pass once
  // their deadline has passed in real time.
  int guard = 0;
  while (target->idle() && guard++ < 10000) {
    ticker.pump();
  }
  auto t = target->pop_control();
  ASSERT_TRUE(t.has_value());
  ASSERT_TRUE(std::holds_alternative<ticker_tick_t>(*t));
  ASSERT_EQUAL(std::get<ticker_tick_t>(*t).hdr.corr, 7ULL);

  ticker.request_stop_token();
  while (ticker.pump()) { /* loop exits, stopped is posted */ }
}
```

Key points:

- `pump()` calls `on_start()` on the first pass, exactly like the threaded
  wrapper — pump-mode actors behave identically to threaded ones, including
  the `stopped`/`actor_died` posting.
- Timers are one-shot; they run when `poller.wait(0)` processes expired
  timers, so a pump loop advances real time (a few ms per test).
- `wait_for` deadlines fire the same way: pump until the resume runs.

## Patterns used in the real tests

**Selective wait (request/response):** post a request with `reply_to = the
actor's mailbox`, pump, then inspect the waiter outcome:

```cpp
actor.mailbox()->post_control(peer_status_req_t{hdr_t{11}, req, 3});
actor.pump();
auto resp = req->pop_control();
ASSERT_TRUE(std::holds_alternative<peer_status_resp_t>(*resp));
```

**Waiting on a typed mailbox:** pop and `std::get_if`/`std::holds_alternative`
over the mailbox's control variant — `std::get_if<X>(&*c)` where `c` is the
`optional<ControlT>` from `pop_control()`.

**Threaded smoke tests** (network/ALSA flows need real threads + real fds):
`start()` the actors, drive with bounded `wait_until(...)` loops (real-time
timeouts), then `request_stop()`:

```cpp
static bool wait_until(const std::function<bool()> &f, int timeout_ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return f();
}
```

**Hardware-dependent tests** (real ALSA sequencer) pass vacuously when the
hardware is absent (e.g. CI containers lack `/dev/snd/seq`):

```cpp
alsa->start();
if (!alsa->seq()) { /* no real sequencer: return early, test passes */ }
```

**Testing a spawn** goes through the router exactly like production:

```cpp
spawn_peer_t sp;
sp.hdr = hdr_t{1};
sp.reply_to = std::make_shared<test_mailbox_t>();
sp.factory = [](const mailbox_handle_t &sup, peer_id_t pid) {
  return std::make_shared<ticker_actor_t>(
      actor_config_t{.name = "t", .id = pid, .supervisor_mailbox = sup});
};
router->mailbox()->post_control(std::move(sp));
// the spawned actor runs on a real thread; wait on its observable state
```

## What to cover

A new actor's tests should exercise:

1. **Each accepted message** → the observable effect (a post, a state change).
2. **The no-lost-wakeup doorbell** (post + pump once = processed).
3. **Drain interleaving** when the actor has a data lane (data-first, control
   advances).
4. **Selective waits**: first-match, non-matching preserved, timeout resume,
   catch-all.
5. **Lifecycle**: graceful stop posts `stopped`; the stop token escalates.
6. **Error isolation**: a throwing handler does not kill the actor
   (`message_exceptions()` counts it).
7. **Hardware flows** (ALSA/network) as threaded smoke tests with graceful
   skips, mirroring `tests/test_alsa_bridge.cpp` (the external-device →
   aseqdump bridging test).
