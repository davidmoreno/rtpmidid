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

#include "lockfree_queue.hpp"
#include <cstdint>

namespace rtpmidid {

/**
 * @short Three-level priority MPSC queue.
 *
 * Backed by three independent @ref mpsc_queue rings (one per priority).
 * `dequeue` drains HIGH first, then NORMAL, then LOW, so producers writing into
 * NORMAL/LOW never starve HIGH-priority traffic.
 *
 * Thread safety:
 * - Any number of producer threads may call `enqueue` (each ring is MPSC).
 * - A single consumer thread must call `dequeue`.
 *
 * Sizes are per-ring power-of-two capacities; defaults bias capacity towards
 * the hot HIGH path (typically MIDI) and keep NORMAL/LOW small.
 */
enum class queue_priority_e : uint8_t {
  HIGH = 0,
  NORMAL = 1,
  LOW = 2,
};

template <typename T, size_t HighSize = 4096, size_t NormalSize = 256,
          size_t LowSize = 64>
class priority_mpsc_queue {
  static_assert((HighSize & (HighSize - 1)) == 0, "HighSize must be power of 2");
  static_assert((NormalSize & (NormalSize - 1)) == 0,
                "NormalSize must be power of 2");
  static_assert((LowSize & (LowSize - 1)) == 0, "LowSize must be power of 2");

  mpsc_queue<T, HighSize> high_;
  mpsc_queue<T, NormalSize> normal_;
  mpsc_queue<T, LowSize> low_;

public:
  priority_mpsc_queue() = default;
  ~priority_mpsc_queue() = default;

  priority_mpsc_queue(const priority_mpsc_queue &) = delete;
  priority_mpsc_queue &operator=(const priority_mpsc_queue &) = delete;
  priority_mpsc_queue(priority_mpsc_queue &&) = delete;
  priority_mpsc_queue &operator=(priority_mpsc_queue &&) = delete;

  bool enqueue(const T &item, queue_priority_e prio) {
    switch (prio) {
    case queue_priority_e::HIGH:
      return high_.enqueue(item);
    case queue_priority_e::NORMAL:
      return normal_.enqueue(item);
    case queue_priority_e::LOW:
      return low_.enqueue(item);
    }
    return false;
  }

  bool enqueue(T &&item, queue_priority_e prio) {
    switch (prio) {
    case queue_priority_e::HIGH:
      return high_.enqueue(std::move(item));
    case queue_priority_e::NORMAL:
      return normal_.enqueue(std::move(item));
    case queue_priority_e::LOW:
      return low_.enqueue(std::move(item));
    }
    return false;
  }

  /** Drain in priority order: HIGH first, then NORMAL, then LOW. */
  bool dequeue(T &item) {
    if (high_.dequeue(item))
      return true;
    if (normal_.dequeue(item))
      return true;
    if (low_.dequeue(item))
      return true;
    return false;
  }

  bool empty() const {
    return high_.empty() && normal_.empty() && low_.empty();
  }

  size_t high_size() const { return high_.size(); }
  size_t normal_size() const { return normal_.size(); }
  size_t low_size() const { return low_.size(); }
};

} // namespace rtpmidid
