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
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <pthread.h>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/shutdown_signals.hpp>
#include <rtpmidid/signal.hpp>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

using namespace rtpmidid;

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

static bool shutdown_signals_masked_on_thread() {
  sigset_t mask {};
  (void)pthread_sigmask(SIG_SETMASK, nullptr, &mask);
  return sigismember(&mask, SIGINT) != 0 && sigismember(&mask, SIGTERM) != 0;
}

void test_worker_thread_blocks_shutdown_signals() {
  block_shutdown_signals();
  std::atomic<bool> worker_blocked{false};
  std::thread worker([&worker_blocked]() {
    block_shutdown_signals();
    worker_blocked = shutdown_signals_masked_on_thread();
  });
  worker.join();
  ASSERT_TRUE(worker_blocked.load());
}

void test_shutdown_eventfd_closes_poller_on_sigint() {
  block_shutdown_signals();

  const int shutdown_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  ASSERT_TRUE(shutdown_eventfd >= 0);

  install_shutdown_signal_handlers(shutdown_eventfd);

  std::atomic<bool> poller_closed{false};
  auto listener = poller.add_fd_in(shutdown_eventfd, [&](int fd) {
    uint64_t n = 0;
    while (read(fd, &n, sizeof n) == static_cast<ssize_t>(sizeof n)) {
    }
    poller.close();
    poller_closed = true;
  });

  unblock_shutdown_signals();

  std::thread sig_sender([]() {
    block_shutdown_signals();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // Process-directed signal is delivered to a thread that does not mask it.
    (void)kill(getpid(), SIGINT);
  });

  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (poller.is_open() && std::chrono::steady_clock::now() < deadline) {
    poller.wait(50ms);
  }

  sig_sender.join();
  listener.stop();
  ::close(shutdown_eventfd);

  ASSERT_TRUE(poller_closed.load());
}

int main(void) {
  test_case_t testcase{
      TEST(test_signal_disconnect),
      TEST(test_worker_thread_blocks_shutdown_signals),
      TEST(test_shutdown_eventfd_closes_poller_on_sigint),
  };

  testcase.run();

  return testcase.exit_code();
}
