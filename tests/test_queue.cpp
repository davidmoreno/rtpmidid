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

/// Unit tests for the queue/mailbox infrastructure (tasks 1.1-1.4):
/// waker_t / eventfd_waker_t, queue_t / mpsc_queue_t, mailbox_t.

#include "mailbox.hpp"
#include "queue.hpp"
#include "test_case.hpp"
#include "waker.hpp"
#include <atomic>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>

// --- global allocation counting (for the no-alloc push test) ----------------
static std::atomic<long> g_allocs{0};

void *operator new(std::size_t n) {
  g_allocs++;
  if (void *p = malloc(n)) {
    return p;
  }
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
  g_allocs++;
  if (void *p = malloc(n)) {
    return p;
  }
  throw std::bad_alloc();
}
void *operator new(std::size_t n, std::align_val_t al) {
  g_allocs++;
  if (void *p = aligned_alloc(static_cast<std::size_t>(al), n)) {
    return p;
  }
  throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t al) {
  g_allocs++;
  if (void *p = aligned_alloc(static_cast<std::size_t>(al), n)) {
    return p;
  }
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, std::size_t) noexcept { free(p); }
void operator delete[](void *p, std::size_t) noexcept { free(p); }
void operator delete(void *p, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
  free(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
  free(p);
}

// --- helper types -----------------------------------------------------------

/// Waker that counts wake() calls instead of writing to an eventfd.
class counting_waker_t : public rtpmididns::waker_t {
public:
  std::atomic<int> wakes{0};
  void wake() noexcept override { wakes++; }
  void prepare() noexcept override {}
  int fd() const noexcept override { return -1; }
};

using test_mailbox_t = rtpmididns::mailbox_t<int, std::string>;

// --- tests ------------------------------------------------------------------

void test_fifo_order_single_producer() {
  counting_waker_t waker;
  rtpmididns::mpsc_queue_t<int> q(waker, 1024, rtpmididns::drop_policy_t::drop_incoming);
  for (int i = 1; i <= 1000; i++) {
    ASSERT_TRUE(q.push(i));
  }
  for (int i = 1; i <= 1000; i++) {
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_EQUAL(*v, i);
  }
  ASSERT_TRUE(q.empty());
}

void test_drop_incoming_policy() {
  counting_waker_t waker;
  rtpmididns::mpsc_queue_t<int> q(waker, 4, rtpmididns::drop_policy_t::drop_incoming);
  for (int i = 1; i <= 4; i++) {
    ASSERT_TRUE(q.push(i));
  }
  // Full: the new element is dropped, no wake.
  ASSERT_FALSE(q.push(5));
  ASSERT_EQUAL(q.drops(), 1ULL);
  // Counter reflects every drop.
  ASSERT_FALSE(q.push(6));
  ASSERT_EQUAL(q.drops(), 2ULL);
  // Contents are the original 1..4.
  for (int i = 1; i <= 4; i++) {
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_EQUAL(*v, i);
  }
  ASSERT_TRUE(q.empty());
  // Room again: push succeeds.
  ASSERT_TRUE(q.push(7));
  ASSERT_EQUAL(q.drops(), 2ULL);
}

void test_drop_oldest_policy() {
  counting_waker_t waker;
  rtpmididns::mpsc_queue_t<int> q(waker, 4, rtpmididns::drop_policy_t::drop_oldest);
  for (int i = 1; i <= 4; i++) {
    ASSERT_TRUE(q.push(i));
  }
  // Full: the oldest is displaced, the new element is stored.
  ASSERT_TRUE(q.push(5));
  ASSERT_EQUAL(q.drops(), 1ULL);
  ASSERT_TRUE(q.push(6));
  ASSERT_EQUAL(q.drops(), 2ULL);
  // Contents are 3,4,5,6 (freshest survive).
  for (int i = 3; i <= 6; i++) {
    auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    ASSERT_EQUAL(*v, i);
  }
  ASSERT_TRUE(q.empty());
}

void test_push_no_alloc() {
  counting_waker_t waker;
  // Constructed before the measured section: the ring preallocates here.
  rtpmididns::mpsc_queue_t<int> q(waker, 1024, rtpmididns::drop_policy_t::drop_oldest);
  g_allocs = 0;
  for (int i = 0; i < 1000; i++) {
    ASSERT_TRUE(q.push(i));
  }
  // Full-queue displacing pushes must not allocate either.
  for (int i = 0; i < 1000; i++) {
    ASSERT_TRUE(q.push(i + 100000));
  }
  ASSERT_EQUAL(q.drops(), 976ULL); // 2000 - 1024
  while (q.try_pop()) {
  }
  ASSERT_EQUAL(g_allocs.load(), 0L);
}

void test_wake_on_enqueue_only() {
  counting_waker_t waker;
  rtpmididns::mpsc_queue_t<int> q(waker, 4, rtpmididns::drop_policy_t::drop_incoming);
  q.push(1);
  ASSERT_EQUAL(waker.wakes.load(), 1);
  q.push(2);
  q.push(3);
  q.push(4);
  ASSERT_EQUAL(waker.wakes.load(), 4);
  // A dropped push does not wake.
  ASSERT_FALSE(q.push(5));
  ASSERT_EQUAL(waker.wakes.load(), 4);
  // A displacing (drop_oldest) push enqueues, so it wakes.
  rtpmididns::mpsc_queue_t<int> q2(waker, 2, rtpmididns::drop_policy_t::drop_oldest);
  q2.push(1);
  q2.push(2);
  ASSERT_TRUE(q2.push(3));
  ASSERT_EQUAL(waker.wakes.load(), 7);
}

void test_coalesced_burst_wake() {
  rtpmididns::eventfd_waker_t w;
  // N wakes while the consumer "sleeps" cost exactly one eventfd write.
  for (int i = 0; i < 100; i++) {
    w.wake();
  }
  uint64_t value = 0;
  ssize_t r = ::read(w.fd(), &value, sizeof(value));
  ASSERT_EQUAL(r, (ssize_t)sizeof(value));
  ASSERT_EQUAL(value, 1ULL);
  // prepare() after the signal was already consumed is a no-op (EAGAIN).
  w.prepare();
  // The flag is cleared: the next wake writes again.
  w.wake();
  value = 0;
  r = ::read(w.fd(), &value, sizeof(value));
  ASSERT_EQUAL(r, (ssize_t)sizeof(value));
  ASSERT_EQUAL(value, 1ULL);
  w.prepare();
}

void test_no_lost_wakeup_race_after_prepare() {
  test_mailbox_t mb;
  mb.prepare(); // nothing pending
  mb.post_data(1); // races the reset: arms the doorbell after the read
  // The post-drain re-check must observe the element...
  ASSERT_FALSE(mb.idle());
  // ...and the doorbell holds the count for the next wait.
  struct pollfd pfd{mb.doorbell_fd(), POLLIN, 0};
  ASSERT_EQUAL(poll(&pfd, 1, 0), 1);
  mb.prepare();
  std::vector<int> got;
  mb.drain_data_first([&](int v) { got.push_back(v); },
                      [](std::string) {});
  ASSERT_EQUAL(got.size(), 1UL);
  ASSERT_TRUE(mb.idle());
}

void test_no_lost_wakeup_race_between_reset_and_recheck() {
  test_mailbox_t mb;
  mb.prepare();
  // Producer enqueues between the doorbell reset and the emptiness
  // re-check: the re-check sees it and the loop does not sleep.
  mb.post_data(42);
  ASSERT_FALSE(mb.idle());
  int got = -1;
  mb.drain_data_first([&](int v) { got = v; }, [](std::string) {});
  ASSERT_EQUAL(got, 42);
  ASSERT_TRUE(mb.idle());
}

void test_threaded_no_lost_wakeup_protocol() {
  constexpr int total = 20000;
  test_mailbox_t mb(total + 16, 16); // data lane bigger than the burst: no drops
  std::atomic<int> received{0};
  std::thread producer([&]() {
    for (int i = 0; i < total; i++) {
      ASSERT_TRUE(mb.post_data(i));
    }
  });
  // Consumer: the design D8 protocol loop.
  while (received.load() < total) {
    mb.prepare();
    mb.drain_data_first(
        [&](int) { received++; }, [](std::string) {});
    if (!mb.idle()) {
      continue; // a push raced the reset: re-drain, don't sleep
    }
    struct pollfd pfd{mb.doorbell_fd(), POLLIN, 0};
    poll(&pfd, 1, 100); // bounded fallback; the doorbell wakes us promptly
  }
  producer.join();
  ASSERT_EQUAL(received.load(), total);
  ASSERT_TRUE(mb.idle());
}

void test_mailbox_drain_interleaving() {
  test_mailbox_t mb;
  mb.post_data(1);
  mb.post_data(2);
  mb.post_data(3);
  mb.post_control("C1");
  mb.post_control("C2");
  std::vector<std::string> order;
  mb.drain_data_first([&](int v) { order.push_back("D" + std::to_string(v)); },
                      [&](std::string s) { order.push_back(std::move(s)); });
  const std::vector<std::string> expected{"D1", "D2", "D3", "C1", "C2"};
  ASSERT_TRUE(order == expected);
}

void test_mailbox_drain_control_progress_under_data_stream() {
  test_mailbox_t mb;
  mb.post_data(1);
  mb.post_control("C1");
  mb.post_control("C2");
  std::vector<std::string> order;
  int data_count = 0;
  // A continuous data stream: each handled data message refills the lane.
  mb.drain_data_first(
      [&](int) {
        data_count++;
        order.push_back("D" + std::to_string(data_count));
        if (data_count < 3) {
          mb.post_data(data_count);
        }
      },
      [&](std::string s) { order.push_back(std::move(s)); });
  // Data is drained first, but control still advances one per pass:
  // neither lane starves.
  const std::vector<std::string> expected{"D1", "D2", "D3", "C1", "C2"};
  ASSERT_TRUE(order == expected);
  ASSERT_TRUE(mb.idle());
}

void test_mailbox_pop_matching_first_match_rest_preserved() {
  test_mailbox_t mb;
  mb.post_control("A");
  mb.post_control("B");
  mb.post_control("C");
  auto m = mb.pop_matching([](const std::string &s) { return s == "B"; });
  ASSERT_TRUE(m.has_value());
  ASSERT_EQUAL(*m, "B");
  // Non-matching messages remain queued in their original order.
  auto a = mb.pop_control();
  ASSERT_TRUE(a.has_value());
  ASSERT_EQUAL(*a, "A");
  auto c = mb.pop_control();
  ASSERT_TRUE(c.has_value());
  ASSERT_EQUAL(*c, "C");
  ASSERT_TRUE(mb.idle());
}

void test_mailbox_pop_matching_no_match() {
  test_mailbox_t mb;
  mb.post_control("A");
  mb.post_control("B");
  auto m = mb.pop_matching([](const std::string &s) { return s == "Z"; });
  ASSERT_FALSE(m.has_value());
  // Nothing was consumed.
  auto a = mb.pop_control();
  ASSERT_TRUE(a.has_value());
  ASSERT_EQUAL(*a, "A");
  ASSERT_FALSE(mb.idle());
}

void test_mailbox_one_doorbell_for_both_lanes() {
  test_mailbox_t mb;
  const int fd = mb.doorbell_fd();
  struct pollfd pfd{fd, POLLIN, 0};
  ASSERT_EQUAL(poll(&pfd, 1, 0), 0); // quiet initially
  mb.post_data(1);
  ASSERT_EQUAL(poll(&pfd, 1, 0), 1); // data lane arms the doorbell
  mb.prepare();
  mb.post_control("hi");
  ASSERT_EQUAL(poll(&pfd, 1, 0), 1); // control lane arms the same doorbell
  mb.prepare();
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_fifo_order_single_producer),
      TEST(test_drop_incoming_policy),
      TEST(test_drop_oldest_policy),
      TEST(test_push_no_alloc),
      TEST(test_wake_on_enqueue_only),
      TEST(test_coalesced_burst_wake),
      TEST(test_no_lost_wakeup_race_after_prepare),
      TEST(test_no_lost_wakeup_race_between_reset_and_recheck),
      TEST(test_threaded_no_lost_wakeup_protocol),
      TEST(test_mailbox_drain_interleaving),
      TEST(test_mailbox_drain_control_progress_under_data_stream),
      TEST(test_mailbox_pop_matching_first_match_rest_preserved),
      TEST(test_mailbox_pop_matching_no_match),
      TEST(test_mailbox_one_doorbell_for_both_lanes),
  };
  testcase.run(argc, argv);
  return testcase.exit_code();
}
