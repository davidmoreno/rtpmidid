/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include "priority_mpsc_queue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rtpmidid {

/**
 * @short Blocking wrapper around @ref priority_mpsc_queue.
 *
 * Pure transport: owns a `condition_variable` + `mutex` so a consumer thread
 * can park itself when the queue is empty and be woken on every enqueue. It
 * intentionally knows nothing about thread lifecycle — start / stop / "should
 * this thread keep running" belong to the thread's owner, not to the queue.
 *
 * Typical use:
 *
 * @code
 *   blocking_priority_queue<command_t> mailbox;
 *
 *   // owner-side state
 *   std::atomic<bool> running_{false};
 *
 *   // consumer thread
 *   command_t cmd;
 *   while (running_.load()) {
 *       if (mailbox.wait_dequeue(cmd))
 *           handle(cmd);
 *       // false = heartbeat timeout: loop re-checks running_
 *   }
 *   while (mailbox.try_dequeue(cmd))  // final drain
 *       handle(cmd);
 *
 *   // producer (any thread)
 *   mailbox.enqueue(std::move(cmd), queue_priority_e::HIGH);
 *
 *   // shutdown (any thread): push a typed message, the consumer's handler
 *   // flips `running_`, the loop exits on the next iteration.
 *   mailbox.enqueue(command_t{shutdown_t{}}, queue_priority_e::NORMAL);
 * @endcode
 *
 * The hot path stays lock-free: while items are arriving, `wait_dequeue` only
 * touches the underlying lock-free ring; the mutex is acquired only when the
 * queue is empty and the thread is about to park.
 *
 * Thread safety mirrors @ref priority_mpsc_queue: any number of producers,
 * exactly one consumer.
 */
template <typename T, size_t HighSize = 4096, size_t NormalSize = 256,
          size_t LowSize = 64>
class blocking_priority_queue {
public:
  using duration = std::chrono::milliseconds;

  blocking_priority_queue() = default;
  ~blocking_priority_queue() = default;

  blocking_priority_queue(const blocking_priority_queue &) = delete;
  blocking_priority_queue &operator=(const blocking_priority_queue &) = delete;
  blocking_priority_queue(blocking_priority_queue &&) = delete;
  blocking_priority_queue &operator=(blocking_priority_queue &&) = delete;

  /**
   * Heartbeat used by `wait_dequeue` when the queue is empty. Producers wake
   * the consumer immediately via `notify_one()`; this timeout only bounds the
   * worst-case latency for the consumer to re-check its own external state
   * (e.g. a `running` flag) when no message has arrived. Default: 100ms.
   */
  void set_timeout(duration t) { idle_timeout_ = t; }
  duration timeout() const { return idle_timeout_; }

  /** Enqueue (non-blocking). Returns false if the priority ring is full. */
  bool enqueue(const T &item, queue_priority_e prio) {
    if (!queue_.enqueue(item, prio))
      return false;
    cv_.notify_one();
    return true;
  }

  bool enqueue(T &&item, queue_priority_e prio) {
    if (!queue_.enqueue(std::move(item), prio))
      return false;
    cv_.notify_one();
    return true;
  }

  /**
   * Block until an item is available, `wake()` is called, or the heartbeat
   * fires.
   *
   * @return true if @a out was filled with a real item.
   * @return false on heartbeat timeout or external `wake()`. The caller is
   *         expected to re-check whatever external state it cares about
   *         (typically its own `running` flag).
   *
   * Spurious wakeups re-loop internally, so a `true` return always carries
   * a real item.
   */
  bool wait_dequeue(T &out) {
    if (queue_.dequeue(out))
      return true;
    if (wake_flag_.exchange(false))
      return false;
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, idle_timeout_, [this] {
      return !queue_.empty() || wake_flag_.load();
    });
    wake_flag_.store(false);
    lk.unlock();
    return queue_.dequeue(out);
  }

  /** Non-blocking dequeue. Returns false if the queue is empty. */
  bool try_dequeue(T &out) { return queue_.dequeue(out); }

  /**
   * Wake every parked consumer without enqueueing anything. Useful when the
   * owner wants the consumer to re-evaluate external state immediately and
   * there is no natural message to deliver (e.g. shutdown when the queue is
   * full and `enqueue` of a sentinel would fail).
   */
  void wake() {
    wake_flag_.store(true);
    cv_.notify_all();
  }

  bool empty() const { return queue_.empty(); }
  size_t high_size() const { return queue_.high_size(); }
  size_t normal_size() const { return queue_.normal_size(); }
  size_t low_size() const { return queue_.low_size(); }

private:
  priority_mpsc_queue<T, HighSize, NormalSize, LowSize> queue_;
  mutable std::mutex mtx_;
  std::condition_variable cv_;
  std::atomic<bool> wake_flag_{false};
  duration idle_timeout_{100};
};

} // namespace rtpmidid
