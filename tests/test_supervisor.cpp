/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// Supervisor actor tests (task 6.6): reaper joins delegated jthreads
/// off-loop; ordered shutdown stops managed actors and completes.

#include "actor.hpp"
#include "supervisor_actor.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

using namespace rtpmididns;

static bool wait_until(const std::function<bool()> &f, int timeout_ms = 5000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (f()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}

class stoppable_actor_t : public actor_t<std::monostate, test_control_t> {
public:
  std::atomic<int> on_stop_calls{0};
  std::atomic<bool> stopped{false};
  using actor_t::actor_t;
  void on_stop() override { on_stop_calls++; }
};

void test_reaper_joins_delegated_threads() {
  auto supervisor = std::make_shared<supervisor_actor_t>(
      actor_config_t{.name = "sup", .supervisor_mailbox =
                                         std::make_shared<test_mailbox_t>()});
  supervisor->pump(); // on_start: the reaper thread starts
  std::atomic<bool> finished{false};
  std::jthread t([&finished] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    finished = true;
  });
  supervisor->mailbox()->post_control(reap_actor_t{std::move(t), "test"});
  // The reaper joins the thread off-loop; the thread runs to completion.
  ASSERT_TRUE(wait_until([&] { return finished.load(); }));
  supervisor->request_stop_token();
  while (supervisor->pump()) {
  }
}

void test_ordered_shutdown_stops_managed() {
  auto sup_mb = std::make_shared<test_mailbox_t>();
  auto supervisor = std::make_shared<supervisor_actor_t>(
      actor_config_t{.name = "sup", .supervisor_mailbox = sup_mb});
  auto a1 = std::make_shared<stoppable_actor_t>(actor_config_t{
      .name = "a1", .supervisor_mailbox = supervisor->mailbox()});
  auto a2 = std::make_shared<stoppable_actor_t>(actor_config_t{
      .name = "a2", .supervisor_mailbox = supervisor->mailbox()});
  supervisor->add_managed(a1);
  supervisor->add_managed(a2);

  supervisor->request_shutdown();
  // Pump everything (pump mode): the supervisor posts stop; the managed
  // actors process it and post stopped; the supervisor finalizes.
  int guard = 0;
  while (!supervisor->shutdown_complete() && guard++ < 200000) {
    supervisor->pump();
    a1->pump();
    a2->pump();
  }
  ASSERT_TRUE(supervisor->shutdown_complete());
  ASSERT_TRUE(a1->on_stop_calls.load() >= 1);
  ASSERT_TRUE(a2->on_stop_calls.load() >= 1);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_reaper_joins_delegated_threads),
      TEST(test_ordered_shutdown_stops_managed),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}
