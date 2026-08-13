# Creating a new actor

This page is a complete recipe for adding a new actor. It walks through a
worked example — a small **ticker actor** that emits periodic "tick" messages
to a configured target — and lists the rules that apply to every actor.

## The recipe

1. **Decide the message set** the new actor accepts (and which messages it
   posts to others).
2. **Create the message header** next to the actor: the message structs, the
   actor's `*_control_t` variant, and (if handy) the mailbox alias.
3. **Create the actor class**: `actor_t<DataT, ControlT>` with
   `using control_messages = ...`, the hooks, the handlers.
4. **Wire it in**: supervise it, or spawn it through the router if it is a
   peer.
5. **Test it** in threadless pump mode (see [testing.md](testing.md)).

## Step 1 + 2: the message set

Each actor declares its accepted control messages as a `std::variant`. Put the
structs in the actor's own header (there is no central message file):

```cpp
// ticker_messages.hpp
#pragma once
#include "mailbox.hpp"        // mailbox_t / mailbox_handle_t
#include "message_core.hpp"   // hdr_t, stop_t, mailbox_handle_t

namespace rtpmididns {

/// Configure the ticker: emit a tick to `target` every `period_ms`.
struct ticker_set_t {
  hdr_t hdr;
  std::chrono::milliseconds period{1000};
  mailbox_handle_t target;
};

/// A tick emitted to the configured target (echoes the set request's corr).
struct ticker_tick_t {
  hdr_t hdr;
  uint64_t sequence = 0;
};

/// The ticker's accepted control messages.
using ticker_control_t = std::variant<stop_t, ticker_set_t>;
/// The ticker's mailbox (no data lane: DataT = std::monostate).
using ticker_mailbox_t = mailbox_t<std::monostate, ticker_control_t>;

} // namespace rtpmididns
```

Notes:

- `stop_t` is **always** accepted by every actor (handled by the wrapper, never
  dispatched to `on_control`), so it is in every control variant.
- The data lane element is `data_message_t` only for MIDI actors; actors
  without a data lane use `std::monostate`.
- A message another actor posts to *this* actor must be in this variant; a
  message *this* actor posts elsewhere must be in the target's variant (the
  typed post enforces it at compile time; the erased path enforces it at
  runtime with a loud warning).

## Step 3: the actor class

```cpp
// ticker_actor.hpp
#pragma once
#include "actor.hpp"
#include "ticker_messages.hpp"

namespace rtpmididns {

class ticker_actor_t : public actor_t<std::monostate, ticker_control_t> {
public:
  using control_messages = ticker_control_t; // declared accepted messages
  using actor_t::actor_t;                    // inherit the config ctor

  void on_control(ticker_control_t &&msg) override {
    std::visit(
        [this](auto &&m) {
          using T = std::decay_t<decltype(m)>;
          if constexpr (std::is_same_v<T, ticker_set_t>) {
            configure(m);
          }
          // stop_t is handled by the actor wrapper.
        },
        std::move(msg));
  }
  void on_stop() override { timer_.disable(); }

private:
  void configure(const ticker_set_t &s) {
    target_ = s.target;
    period_ = s.period;
    corr_ = s.hdr.corr;
    timer_ = add_timer(period_, [this] { tick(); });
  }
  void tick() {
    if (target_) {
      target_.post_control(ticker_tick_t{hdr_t{corr_}, ++seq_});
    }
    timer_ = add_timer(period_, [this] { tick(); }); // one-shot: re-arm
  }

  mailbox_handle_t target_;
  std::chrono::milliseconds period_{1000};
  rtpmidid::poller_t::timer_t timer_;
  uint64_t seq_ = 0;
  uint64_t corr_ = 0;
};

} // namespace rtpmididns
```

The API surface you can use (see [lifecycle.md](lifecycle.md) for details):

- **Hooks**: `on_start()`, `on_stop()`, `on_data(DataT&&)`, `on_control(ControlT&&)`,
  `on_loop()`.
- **Lifecycle**: `start()`, `request_stop()`, `request_stop_token()`, `pump()`,
  `finish()`, `take_thread()`.
- **I/O**: `add_fd_in/out/inout(fd, handler)`, `add_timer(ms, handler)`,
  `poller()` (to host lib components like `aseq_t`/`mdns_rtpmidi_t` in your
  own poller).
- **Selective wait**: `wait_for(predicate, deadline, resume)`.
- **Mailbox**: `mailbox()` (typed handle to your own mailbox), `mailbox_handle()`
  (type-erased, for `reply_to`), `set_mailbox(...)` before `start()` when a
  caller pre-creates your mailbox.

## Step 4: wiring

**A supervised top-level actor** (main.cpp):

```cpp
auto ticker = std::make_shared<ticker_actor_t>(
    actor_config_t{.name = "ticker", .supervisor_mailbox = sup_mb});
supervisor->add_managed(ticker); // participates in ordered shutdown
ticker->start();

// configure it from anywhere:
ticker->mailbox()->post_control(
    ticker_set_t{hdr_t{1}, 250ms, some_target_mailbox});
```

**A router-managed peer** is spawned through the router with a prepared
move-only bundle (the router owns the thread and the lifecycle):

```cpp
spawn_peer_t sp;
sp.type = "ticker_peer";
sp.factory = [](const mailbox_handle_t &supervisor, peer_id_t id) {
  return std::make_shared<ticker_actor_t>(
      actor_config_t{.name = "ticker-peer", .id = id,
                     .supervisor_mailbox = supervisor});
};
router->mailbox()->post_control(std::move(sp));
```

Rules for factories: they run on the **router thread** — capture **values**
(name, addresses, handles), never `this`. The actor is born knowing its id
(`peer_id_t`), and the router posts `registered{ids}` before it handles wire
traffic (the `registered` gate).

## Rules that apply to every actor

1. **All state is thread-confined.** Fds, timers and handlers live on the
   actor's own thread; the only cross-thread API is `post_*` to your mailbox.
2. **Handlers are commit-only and fast.** Fallible or blocking work happens in
   the *caller* before posting, or is delegated to the worker (DNS etc.) —
   the router's handlers are the model: O(1) map mutations and mailbox pushes,
   never I/O.
3. **Never join a thread in your own loop (R1).** Wait for `stopped`, or
   delegate to the reaper.
4. **Every wait has a deadline (R3).** Selective waits are mandatory
   deadline-bounded; busy actors use `pending_request_table_t`.
5. **Posting to a terminated actor is safe.** The mailbox lives until the last
   `shared_ptr` handle is dropped; messages into an undrained mailbox are
   bounded and discarded with it.
6. **Own your payloads.** Data-plane messages carry inline MIDI (no
   allocation); control messages may allocate freely.
7. **Message drops are loud.** If your actor's queue fills (a flood) or a
   message is misrouted, the mailbox layer logs it — don't swallow errors.
