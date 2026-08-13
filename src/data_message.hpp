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

/// The data plane (spec: actor-message-protocol): the two data-plane
/// message kinds (`midi_received`, `midi_to_wire`) with self-owning inline
/// MIDI payloads and the bounded heap escape pool for oversized payloads.

#pragma once

#include "message_core.hpp"
#include "rtpmidid/logger.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>

namespace rtpmididns {

// ---------------------------------------------------------------------------
// Bounded heap escape pool for oversized MIDI payloads (design D6).
// Allocation is permitted up to a configurable byte budget; when the pool
// is exhausted the payload is dropped with a rate-limited log and the drop
// counter increments. Oversized MIDI is rare (rawmidi sysex floods), so
// this path stays off the hot path by construction.
// ---------------------------------------------------------------------------
class escape_pool_t {
public:
  static inline std::atomic<size_t> used_bytes{0};
  static inline std::atomic<uint64_t> dropped_payloads{0};
  static inline size_t limit = 4 * 1024 * 1024; // configured budget, 0 = unlimited

  static bool acquire(size_t n) {
    if (limit == 0) {
      used_bytes.fetch_add(n);
      return true;
    }
    size_t used = used_bytes.load();
    while (used + n <= limit) {
      if (used_bytes.compare_exchange_weak(used, used + n)) {
        return true;
      }
    }
    dropped_payloads.fetch_add(1);
    WARNING_RATE_LIMIT(5, "MIDI escape pool exhausted ({} bytes used, "
                          "{} bytes needed): oversized payload dropped.",
                       used, n);
    return false;
  }
  static void release(size_t n) { used_bytes.fetch_sub(n); }
};

/**
 * Owned MIDI payload: inline fixed storage (>= 1536 bytes, covering
 * MTU-sized RTP-MIDI packets) plus a bounded heap escape for oversized
 * payloads. Copying/moving across queues is self-owning: the payload stays
 * valid after the producer is destroyed.
 */
class midi_payload_t {
public:
  static constexpr size_t inline_capacity = 1536;

  midi_payload_t() = default;
  midi_payload_t(const midi_payload_t &other) : size_(other.size_) {
    if (other.heap_) {
      if (!escape_pool_t::acquire(other.size_)) {
        // Budget exhausted: copying is impossible; drop (the per-message
        // exception isolation in the actor handles this).
        throw std::runtime_error("midi escape pool exhausted on copy");
      }
      heap_ = std::make_unique<uint8_t[]>(other.size_);
      std::memcpy(heap_.get(), other.heap_.get(), other.size_);
    } else {
      inline_data_ = other.inline_data_;
    }
  }
  midi_payload_t &operator=(const midi_payload_t &other) {
    if (this != &other) {
      release_escape();
      size_ = other.size_;
      if (other.heap_) {
        if (!escape_pool_t::acquire(other.size_)) {
          throw std::runtime_error("midi escape pool exhausted on copy");
        }
        heap_ = std::make_unique<uint8_t[]>(other.size_);
        std::memcpy(heap_.get(), other.heap_.get(), other.size_);
      } else {
        inline_data_ = other.inline_data_;
      }
    }
    return *this;
  }
  midi_payload_t(midi_payload_t &&other) noexcept
      : inline_data_(std::move(other.inline_data_)), heap_(std::move(other.heap_)),
        size_(other.size_) {
    other.size_ = 0;
  }
  midi_payload_t &operator=(midi_payload_t &&other) noexcept {
    if (this != &other) {
      release_escape();
      inline_data_ = std::move(other.inline_data_);
      heap_ = std::move(other.heap_);
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }
  ~midi_payload_t() { release_escape(); }

  /// Build a payload from a byte range. Payloads within the inline
  /// capacity never allocate. Oversized payloads use the heap escape pool;
  /// when the pool is exhausted the payload is dropped (nullopt).
  static std::optional<midi_payload_t> make(const uint8_t *data, size_t size) {
    midi_payload_t p;
    if (!p.assign(data, size)) {
      return std::nullopt;
    }
    return p;
  }

  bool assign(const uint8_t *data, size_t size) {
    release_escape();
    size_ = size;
    if (size <= inline_capacity) {
      std::memcpy(inline_data_.data(), data, size);
    } else {
      if (!escape_pool_t::acquire(size)) {
        size_ = 0;
        return false;
      }
      heap_ = std::make_unique<uint8_t[]>(size);
      std::memcpy(heap_.get(), data, size);
    }
    return true;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  const uint8_t *data() const {
    return heap_ ? heap_.get() : inline_data_.data();
  }
  uint8_t *data() { return heap_ ? heap_.get() : inline_data_.data(); }

private:
  void release_escape() {
    if (heap_) {
      escape_pool_t::release(size_);
      heap_.reset();
    }
  }

  std::array<uint8_t, inline_capacity> inline_data_{};
  std::unique_ptr<uint8_t[]> heap_;
  size_t size_ = 0;
};

/**
 * Data-plane message: exactly two kinds (design D6), no variant overhead;
 * fixed-size with inline MIDI payload. `midi_received{from, payload}` goes
 * peer -> router; `midi_to_wire{to, from, payload}` goes router -> peer;
 * `to` lets one actor host multiple peer ids (e.g. ALSA ports).
 */
struct data_message_t {
  enum class kind_t : uint8_t { midi_received, midi_to_wire };

  kind_t kind = kind_t::midi_received;
  peer_id_t from = 0;
  peer_id_t to = 0;
  midi_payload_t payload;

  data_message_t() = default;

  static data_message_t midi_received(peer_id_t from, midi_payload_t &&payload) {
    data_message_t m;
    m.kind = kind_t::midi_received;
    m.from = from;
    m.payload = std::move(payload);
    return m;
  }
  static data_message_t midi_to_wire(peer_id_t to, peer_id_t from,
                                     midi_payload_t &&payload) {
    data_message_t m;
    m.kind = kind_t::midi_to_wire;
    m.to = to;
    m.from = from;
    m.payload = std::move(payload);
    return m;
  }
};

} // namespace rtpmididns
