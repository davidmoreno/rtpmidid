/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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
#include <rtpmidid/poller.hpp>
using namespace std::chrono_literals;

rtpmidid::poller_t poller;

/// To check for bug https://github.com/davidmoreno/rtpmidid/issues/39
void test_timer_event_order() {
  int state = 0;
  poller.add_timer_event(10ms, [&state] {
    DEBUG("1st Poller {}", state);
    ASSERT_EQUAL(state, 0);
    state = 1;
  });
  poller.add_timer_event(20ms, [&state] {
    DEBUG("2nd Poller {}", state);
    ASSERT_EQUAL(state, 1);
    state = 2;
  });

  while (state != 2) {
    poller.wait();
  }
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_timer_event_order),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
