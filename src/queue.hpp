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

#pragma once

#include "waker.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <pthread.h>
#include <utility>
#include <vector>

namespace rtpmididns {

/// Drop policies, named by what is dropped on a full queue.
enum class drop_policy_t { drop_incoming, drop_oldest };

/**
 * Queue interface (design D5/D14).
 *
 * All queue users go through this interface so the implementation can be
 * swapped (mutex v1 now, SPSC ring / lock-free MPSC later) without touching
 * producers or consumers.
 */
template <typename T> class queue_t {
public:
  virtual ~queue_t() = default;

  /// Enqueue. Never blocks, never allocates. Returns true when the element
  /// was enqueued, false when it was dropped per the configured policy.
  virtual bool push(T &&item) noexcept = 0;
  /// Pop the oldest element, or nullopt when empty.
  virtual std::optional<T> try_pop() = 0;
  virtual size_t capacity() const noexcept = 0;
  /// Total number of dropped elements (safe for concurrent reads).
  virtual uint64_t drops() const noexcept = 0;
  virtual bool empty() const noexcept = 0;
};

/**
 * v1 MPSC queue: one `PTHREAD_PRIO_INHERIT` mutex + a preallocated fixed
 * ring. Construction-time capacity and drop policy; atomic drop counter;
 * wake-on-enqueue happens after unlocking and only for enqueued elements.
 *
 * The ring is preallocated to capacity at construction so `push` never
 * allocates (design D3/D14: bounded, allocation-free hot path).
 * Per-producer FIFO ordering is preserved; no cross-producer guarantees.
 */
template <typename T> class mpsc_queue_t final : public queue_t<T> {
public:
  mpsc_queue_t(waker_t &waker, size_t capacity, drop_policy_t policy)
      : waker_(waker), capacity_(capacity), policy_(policy), buffer_(capacity) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&mutex_, &attr);
    pthread_mutexattr_destroy(&attr);
  }
  ~mpsc_queue_t() override { pthread_mutex_destroy(&mutex_); }

  mpsc_queue_t(const mpsc_queue_t &) = delete;
  mpsc_queue_t &operator=(const mpsc_queue_t &) = delete;
  mpsc_queue_t(mpsc_queue_t &&) = delete;
  mpsc_queue_t &operator=(mpsc_queue_t &&) = delete;

  bool push(T &&item) noexcept override {
    bool enqueued = false;
    {
      pthread_mutex_lock(&mutex_);
      if (size_.load(std::memory_order_relaxed) < capacity_) {
        buffer_[tail()] = std::move(item);
        size_.fetch_add(1, std::memory_order_relaxed);
        enqueued = true;
      } else {
        switch (policy_) {
        case drop_policy_t::drop_incoming:
          // Full: the new element is dropped.
          drops_.fetch_add(1, std::memory_order_relaxed);
          break;
        case drop_policy_t::drop_oldest:
          // Full: displace the oldest element (sits at head_) and store the
          // new one in its slot; advance head_ so the new element becomes
          // the newest at the tail.
          buffer_[head_] = std::move(item);
          head_ = (head_ + 1) % capacity_;
          drops_.fetch_add(1, std::memory_order_relaxed);
          enqueued = true;
          break;
        }
      }
      pthread_mutex_unlock(&mutex_);
    }
    if (enqueued) {
      // Never notify while holding the lock.
      waker_.wake();
    }
    return enqueued;
  }

  bool push(const T &item) noexcept {
    // Non-virtual convenience for copyable element types; producers of
    // move-only lane types (e.g. control messages) push rvalues.
    T copy = item;
    return push(std::move(copy));
  }

  std::optional<T> try_pop() override {
    std::optional<T> result;
    {
      pthread_mutex_lock(&mutex_);
      if (size_.load(std::memory_order_relaxed) > 0) {
        result.emplace(std::move(buffer_[head_]));
        head_ = (head_ + 1) % capacity_;
        size_.fetch_sub(1, std::memory_order_relaxed);
      }
      pthread_mutex_unlock(&mutex_);
    }
    return result;
  }

  size_t capacity() const noexcept override { return capacity_; }
  uint64_t drops() const noexcept override {
    return drops_.load(std::memory_order_relaxed);
  }
  bool empty() const noexcept override {
    return size_.load(std::memory_order_relaxed) == 0;
  }

  /// Pop and handle every element currently queued. Returns the number of
  /// elements handled. The free-function drain helper of design D5.
  template <typename F> size_t drain(F &&handler) {
    size_t n = 0;
    while (auto item = try_pop()) {
      handler(std::move(*item));
      n++;
    }
    return n;
  }

  /**
   * Selective pop (design D15): consumes exactly the first element matching
   * the predicate, scanning in queue order; non-matching elements remain
   * queued in their original order. O(n) with moves; used on control lanes
   * where selective waits are rare and messages are small.
   */
  template <typename Pred> std::optional<T> try_pop_matching(Pred &&pred) {
    std::optional<T> result;
    {
      pthread_mutex_lock(&mutex_);
      const auto n = size_.load(std::memory_order_relaxed);
      for (size_t i = 0; i < n; i++) {
        const auto pos = (head_ + i) % capacity_;
        if (pred(buffer_[pos])) {
          result.emplace(std::move(buffer_[pos]));
          // Shift the elements after the match one slot left, preserving
          // their original relative order.
          for (size_t j = i + 1; j < n; j++) {
            const auto dst = (head_ + j - 1) % capacity_;
            const auto src = (head_ + j) % capacity_;
            buffer_[dst] = std::move(buffer_[src]);
          }
          size_.fetch_sub(1, std::memory_order_relaxed);
          break;
        }
      }
      pthread_mutex_unlock(&mutex_);
    }
    return result;
  }

private:
  size_t tail() const noexcept { return (head_ + size_.load()) % capacity_; }

  waker_t &waker_;
  size_t capacity_;
  drop_policy_t policy_;
  pthread_mutex_t mutex_;
  size_t head_ = 0;
  std::atomic<size_t> size_{0};
  std::vector<T> buffer_;
  std::atomic<uint64_t> drops_{0};
};

/**
 * Future SPSC queue: bounded ring + per-slot sequence counters, exactly one
 * producer and one consumer thread, same waker binding. Interface reserved
 * (non-goal in this change: lock-free queues); drop-in replacement for
 * peer data lanes.
 */
template <typename T> class spsc_queue_t final : public queue_t<T> {
public:
  spsc_queue_t(waker_t &, size_t, drop_policy_t) {} // interface reservation
  bool push(T &&) noexcept override { return false; }
  std::optional<T> try_pop() override { return std::nullopt; }
  size_t capacity() const noexcept override { return 0; }
  uint64_t drops() const noexcept override { return 0; }
  bool empty() const noexcept override { return true; }
};

} // namespace rtpmididns
