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

#include "./test_case.hpp"
#include <chrono>
#include <ratio>
#include <rtpmidid/poller.hpp>
using namespace std::chrono_literals;

/// To check for bug https://github.com/davidmoreno/rtpmidid/issues/39
void test_timer_event_order() {
  int state = 0;
  auto t1 = rtpmidid::poller.add_timer_event(10ms, [&state] {
    DEBUG("1st Poller {}", state);
    ASSERT_EQUAL(state, 0);
    state = 1;
  });
  auto t2 = rtpmidid::poller.add_timer_event(20ms, [&state] {
    DEBUG("2nd Poller {}", state);
    ASSERT_EQUAL(state, 1);
    state = 2;
  });
  {
    auto t3 = rtpmidid::poller.add_timer_event(5ms, [] {
      FAIL("Should never be called. RTTI should have removed it");
    });
  }

  while (state != 2) {
    rtpmidid::poller.wait();
  }
}

int32_t to_ms(std::chrono::milliseconds ms) { return ms.count(); }

template <typename T> int32_t to_ms(T mc) {
  return to_ms(std::chrono::duration_cast<std::chrono::milliseconds>(mc));
}
void test_wait_ms() {
  for (auto ms : {10, 20, 50, 200, 500}) {
    auto start = std::chrono::steady_clock::now();
    auto pre = std::chrono::steady_clock::now() - start;
    bool called = false;
    auto timer = rtpmidid::poller.add_timer_event(
        std::chrono::milliseconds(ms), [&start, &pre, &ms, &called] {
          auto post = std::chrono::steady_clock::now() - start;
          DEBUG("TIMER W {} - {} = {}", to_ms(pre), to_ms(post),
                to_ms(post - pre));

          ASSERT_GTE(to_ms(post - pre), ms);
          called = true;
        });

    auto post = pre;

    // do active waiting
    while (true) {
      DEBUG("Wait");
      rtpmidid::poller.wait(2000ms);
      post = std::chrono::steady_clock::now() - start;
      DEBUG("{} ms passed since last wait", to_ms(post - pre));
      if (to_ms(post - pre) >= ms)
        break;
    }
    DEBUG("POLLER W {} - {} = {}", to_ms(pre), to_ms(post), to_ms(post - pre));
    ASSERT_LT(to_ms(post - pre), 1000);

    ASSERT_GTE(to_ms(post - pre), ms);
    ASSERT_TRUE(called);
  }
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_timer_event_order),
      TEST(test_wait_ms),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
