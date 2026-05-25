/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
#include <atomic>
#include <chrono>
#include <rtpmidid/blocking_priority_queue.hpp>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

using mailbox_t = rtpmidid::blocking_priority_queue<int, 16, 8, 4>;

/** An empty queue returns false from wait_dequeue (heartbeat timeout). */
void test_empty_returns_false_on_timeout() {
  mailbox_t mbox;
  mbox.set_timeout(20ms);
  ASSERT_EQUAL(mbox.timeout().count(), 20);

  int out = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const bool got = mbox.wait_dequeue(out);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count();
  ASSERT_FALSE(got);
  // Should have waited approximately the heartbeat (allow some slack).
  ASSERT_GTE(elapsed_ms, 15);
  ASSERT_LT(elapsed_ms, 500);
}

/** Enqueue + try_dequeue: pure non-blocking path. */
void test_try_dequeue_priority_order() {
  mailbox_t mbox;
  ASSERT_TRUE(mbox.enqueue(30, rtpmidid::queue_priority_e::LOW));
  ASSERT_TRUE(mbox.enqueue(20, rtpmidid::queue_priority_e::NORMAL));
  ASSERT_TRUE(mbox.enqueue(10, rtpmidid::queue_priority_e::HIGH));

  int out = 0;
  ASSERT_TRUE(mbox.try_dequeue(out));
  ASSERT_EQUAL(out, 10);
  ASSERT_TRUE(mbox.try_dequeue(out));
  ASSERT_EQUAL(out, 20);
  ASSERT_TRUE(mbox.try_dequeue(out));
  ASSERT_EQUAL(out, 30);
  ASSERT_FALSE(mbox.try_dequeue(out));
  ASSERT_TRUE(mbox.empty());
}

/** Producer wakes a parked consumer within a small fraction of the timeout. */
void test_wait_dequeue_wakes_on_enqueue() {
  mailbox_t mbox;
  mbox.set_timeout(2s); // big timeout so a missed wakeup would be obvious

  std::atomic<bool> got{false};
  std::atomic<bool> stop{false};
  std::thread consumer([&] {
    int out = 0;
    while (!stop.load()) {
      if (mbox.wait_dequeue(out)) {
        ASSERT_EQUAL(out, 42);
        got.store(true);
        return;
      }
    }
  });

  std::this_thread::sleep_for(20ms);
  const auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(mbox.enqueue(42, rtpmidid::queue_priority_e::HIGH));

  for (int i = 0; i < 200 && !got.load(); ++i) {
    std::this_thread::sleep_for(1ms);
  }
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count();
  ASSERT_TRUE(got.load());
  ASSERT_LT(elapsed_ms, 500); // much faster than the 2s timeout

  stop.store(true);
  mbox.wake();
  consumer.join();
}

/** wake() returns a parked consumer immediately with no message. */
void test_wake_returns_consumer_without_message() {
  mailbox_t mbox;
  mbox.set_timeout(2s);

  std::atomic<bool> returned{false};
  std::atomic<bool> last_got{true};
  std::thread consumer([&] {
    int out = 0;
    last_got.store(mbox.wait_dequeue(out));
    returned.store(true);
  });

  std::this_thread::sleep_for(20ms);
  const auto t0 = std::chrono::steady_clock::now();
  mbox.wake();

  for (int i = 0; i < 200 && !returned.load(); ++i) {
    std::this_thread::sleep_for(1ms);
  }
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count();
  ASSERT_TRUE(returned.load());
  ASSERT_FALSE(last_got.load()); // empty queue → false
  ASSERT_LT(elapsed_ms, 500);

  consumer.join();
}

/** Message-driven shutdown: producer pushes a sentinel, consumer flips
 *  its OWN running flag in the handler, then exits and drains. */
void test_message_driven_shutdown_and_drain() {
  mailbox_t mbox;
  std::atomic<bool> running{true};
  std::vector<int> processed;

  std::thread consumer([&] {
    int cmd = 0;
    while (running.load()) {
      if (mbox.wait_dequeue(cmd)) {
        if (cmd < 0)
          running.store(false); // owner-defined sentinel
        else
          processed.push_back(cmd);
      }
    }
    // Drain
    while (mbox.try_dequeue(cmd)) {
      if (cmd >= 0)
        processed.push_back(cmd);
    }
  });

  // Producer pre-loads work, then sends the sentinel, then keeps pushing
  // more work — everything pre-sentinel must be processed, and items
  // still in flight at sentinel time must be drained.
  ASSERT_TRUE(mbox.enqueue(1, rtpmidid::queue_priority_e::HIGH));
  ASSERT_TRUE(mbox.enqueue(2, rtpmidid::queue_priority_e::HIGH));
  ASSERT_TRUE(mbox.enqueue(3, rtpmidid::queue_priority_e::HIGH));
  ASSERT_TRUE(mbox.enqueue(-1, rtpmidid::queue_priority_e::NORMAL));
  ASSERT_TRUE(mbox.enqueue(4, rtpmidid::queue_priority_e::HIGH));
  ASSERT_TRUE(mbox.enqueue(5, rtpmidid::queue_priority_e::HIGH));

  consumer.join();

  // All five real items must be processed (HIGH drains first; the items
  // pushed after the sentinel were still HIGH-priority and present in the
  // queue when the consumer started draining).
  ASSERT_EQUAL(processed.size(), size_t(5));
  const int expected[] = {1, 2, 3, 4, 5};
  for (size_t i = 0; i < processed.size(); ++i)
    ASSERT_EQUAL(processed[i], expected[i]);
}

/** Many producers, one consumer: nothing lost across a mix of priorities. */
void test_multi_producer_no_loss() {
  using big_mailbox_t = rtpmidid::blocking_priority_queue<int, 1024, 64, 16>;
  big_mailbox_t mbox;
  std::atomic<bool> running{true};

  constexpr int kProducers = 4;
  constexpr int kPerProducer = 200;

  std::vector<int> received;
  received.reserve(kProducers * kPerProducer);
  std::thread consumer([&] {
    int out = 0;
    while (running.load()) {
      if (mbox.wait_dequeue(out)) {
        if (out < 0)
          running.store(false);
        else
          received.push_back(out);
      }
    }
    while (mbox.try_dequeue(out))
      if (out >= 0)
        received.push_back(out);
  });

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&mbox, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        const int value = p * 10000 + i;
        while (!mbox.enqueue(value, rtpmidid::queue_priority_e::HIGH))
          std::this_thread::yield(); // ring full, retry
      }
    });
  }
  for (auto &t : producers)
    t.join();

  std::this_thread::sleep_for(50ms);
  while (!mbox.enqueue(-1, rtpmidid::queue_priority_e::NORMAL))
    std::this_thread::yield();
  consumer.join();

  ASSERT_EQUAL(received.size(), size_t(kProducers * kPerProducer));
}

} // namespace

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_empty_returns_false_on_timeout),
      TEST(test_try_dequeue_priority_order),
      TEST(test_wait_dequeue_wakes_on_enqueue),
      TEST(test_wake_returns_consumer_without_message),
      TEST(test_message_driven_shutdown_and_drain),
      TEST(test_multi_producer_no_loss),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}
