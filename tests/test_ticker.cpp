// Validates the worked example from docs/actors/creating-actors.md.
#include "actor.hpp"
#include "example/ticker_actor.hpp"
#include "example/ticker_messages.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <chrono>

using namespace rtpmididns;

/// The target mailbox the ticker posts into must accept ticker_tick_t.
using tick_target_control_t = std::variant<stop_t, ticker_tick_t>;
using tick_target_mailbox_t = mailbox_t<std::monostate, tick_target_control_t>;

void test_ticker_emits_ticks_to_configured_target() {
  auto target = std::make_shared<tick_target_mailbox_t>();
  ticker_actor_t ticker(actor_config_t{.name = "ticker"});
  ticker.mailbox()->post_control(
      ticker_set_t{hdr_t{7}, std::chrono::milliseconds(5), target});

  int guard = 0;
  while (target->idle() && guard++ < 10000) {
    ticker.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // let the timer age
  }
  auto t = target->pop_control();
  ASSERT_TRUE(t.has_value());
  ASSERT_TRUE(std::holds_alternative<ticker_tick_t>(*t));
  ASSERT_EQUAL(std::get<ticker_tick_t>(*t).hdr.corr, 7ULL);
  ASSERT_EQUAL(std::get<ticker_tick_t>(*t).sequence, 1ULL);

  // The timer keeps re-arming: a second tick arrives.
  guard = 0;
  while (target->idle() && guard++ < 10000) {
    ticker.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(std::holds_alternative<ticker_tick_t>(*target->pop_control()));

  ticker.request_stop_token();
  while (ticker.pump()) {
  }
}

void test_ticker_accepts_only_its_messages() {
  // The typed post to the ticker's own mailbox rejects messages not in its
  // control variant at compile time; via the erased handle it is rejected
  // at runtime (returns false).
  ticker_actor_t ticker(actor_config_t{.name = "ticker"});
  mailbox_handle_t h = ticker.mailbox_handle();
  ASSERT_FALSE(h.post_control(peer_command_t{hdr_t{1}, {}, 0, "x", "{}"}));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_ticker_emits_ticks_to_configured_target),
      TEST(test_ticker_accepts_only_its_messages),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}
