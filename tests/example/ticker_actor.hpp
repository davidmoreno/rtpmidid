// Ticker actor — the worked example from docs/actors/creating-actors.md.
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
