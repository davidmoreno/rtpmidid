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
#include "../tests/test_case.hpp"
#include <memory>
#include <rtpmidid/signal.hpp>

void test_signal_disconnect() {
  signal_t<int> signal;

  int callcount = 0;

  // no store connection, automatically delete
  {
    (void)signal.connect([&callcount](int x) { callcount += x; });

    signal(1);
    ASSERT_EQUAL(callcount, 0);
  }

  // store connection, call works
  {
    auto conn = signal.connect([&callcount](int x) { callcount += x; });

    signal(1);
    ASSERT_EQUAL(callcount, 1);
  }

  // several levels
  {
    callcount = 0;
    auto conn = signal.connect([&callcount](int x) { callcount += x; });

    {
      auto conn = signal.connect([&callcount](int x) { callcount += x; });
      {
        auto conn = signal.connect([&callcount](int x) { callcount += x; });
      }
      signal(10);
    }

    ASSERT_EQUAL(callcount, 20);
  }

  // copy connection
  {
    callcount = 0;

    connection_t<int> conn;
    {
      conn = signal.connect([&callcount](int x) { callcount += x; });
    }

    signal(2);
    ASSERT_EQUAL(callcount, 2)
  }
  // copy connection
  {
    callcount = 0;
    connection_t<int> conn0;
    connection_t<int> conn2(std::move(conn0));
    {
      auto conn = signal.connect([&callcount](int x) { callcount += x; });
      conn2 = std::move(conn);
    }

    signal(3);
    ASSERT_EQUAL(callcount, 3)
  }

  // move construt
  {
    callcount = 0;
    connection_t<int> *conn2 = nullptr;
    {
      auto conn = signal.connect([&callcount](int x) { callcount += x; });
      conn2 = new connection_t(std::move(conn));
    }

    signal(4);
    ASSERT_EQUAL(callcount, 4)
    delete conn2;
  }
}

int main(void) {
  test_case_t testcase{
      TEST(test_signal_disconnect),
  };

  testcase.run();

  return testcase.exit_code();
}
