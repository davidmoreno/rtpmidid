/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

namespace rtpmidid {

/**
 * @short Lock-free single-producer single-consumer (SPSC) queue
 *
 * This is a lock-free ring buffer queue optimized for high-frequency
 * MIDI data paths. It uses atomic operations and memory barriers for
 * thread-safe enqueue/dequeue operations.
 *
 * Thread safety:
 * - One thread can enqueue (producer)
 * - One thread can dequeue (consumer)
 * - These must be different threads
 *
 * @tparam T The type of items stored in the queue
 * @tparam Size The size of the ring buffer (must be power of 2)
 */
template <typename T, size_t Size>
class lockfree_queue {
  static_assert((Size & (Size - 1)) == 0,
               "Size must be a power of 2 for efficient modulo");

private:
  alignas(64) std::array<T, Size> buffer;
  alignas(64) std::atomic<size_t> head{0}; // Consumer index
  alignas(64) std::atomic<size_t> tail{0}; // Producer index

  // Mask for efficient modulo (Size is power of 2)
  static constexpr size_t mask = Size - 1;

  size_t next_index(size_t idx) const { return (idx + 1) & mask; }

public:
  lockfree_queue() = default;
  ~lockfree_queue() = default;

  // Non-copyable, non-movable
  lockfree_queue(const lockfree_queue &) = delete;
  lockfree_queue &operator=(const lockfree_queue &) = delete;
  lockfree_queue(lockfree_queue &&) = delete;
  lockfree_queue &operator=(lockfree_queue &&) = delete;

  /**
   * @brief Enqueue an item (non-blocking, producer thread only)
   *
   * @param item The item to enqueue
   * @return true if enqueued successfully, false if queue is full
   */
  bool enqueue(const T &item) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = next_index(current_tail);
    const size_t current_head = head.load(std::memory_order_acquire);

    // Check if queue is full
    if (next_tail == current_head) {
      return false;
    }

    // Write item to buffer
    // For types with vectors, this will copy the vector (which is safe)
    buffer[current_tail] = item;

    // Update tail with release semantics (ensures item is visible before tail)
    tail.store(next_tail, std::memory_order_release);
    return true;
  }

  /**
   * @brief Enqueue an item by move (non-blocking, producer thread only)
   */
  bool enqueue(T &&item) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = next_index(current_tail);
    const size_t current_head = head.load(std::memory_order_acquire);

    if (next_tail == current_head) {
      return false;
    }

    // Use move assignment - this is safe because we know the slot is not being read
    buffer[current_tail] = std::move(item);
    
    // Ensure memory barrier before updating tail
    std::atomic_thread_fence(std::memory_order_release);
    tail.store(next_tail, std::memory_order_release);
    return true;
  }

  /**
   * @brief Dequeue an item (non-blocking, consumer thread only)
   *
   * @param item Reference to store the dequeued item
   * @return true if item was dequeued, false if queue is empty
   */
  bool dequeue(T &item) {
    const size_t current_head = head.load(std::memory_order_relaxed);
    const size_t current_tail = tail.load(std::memory_order_acquire);

    // Check if queue is empty
    if (current_head == current_tail) {
      return false;
    }

    // Read item from buffer using move
    // This is safe because we know the slot has been written (head != tail)
    // and won't be written again until we advance head
    item = std::move(buffer[current_head]);

    // Update head with release semantics (must happen after move)
    head.store(next_index(current_head), std::memory_order_release);
    return true;
  }

  /**
   * @brief Check if queue is empty (approximate, for monitoring only)
   *
   * Note: This is a snapshot and may be stale immediately after checking.
   * Do not use for synchronization.
   */
  bool empty() const {
    return head.load(std::memory_order_relaxed) ==
           tail.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get approximate number of items in queue (for monitoring)
   */
  size_t size() const {
    const size_t h = head.load(std::memory_order_relaxed);
    const size_t t = tail.load(std::memory_order_relaxed);
    if (t >= h) {
      return t - h;
    }
    return Size - (h - t);
  }

  /**
   * @brief Get maximum capacity
   */
  static constexpr size_t capacity() { return Size - 1; }
};

} // namespace rtpmidid
