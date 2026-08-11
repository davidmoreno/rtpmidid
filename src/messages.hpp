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

/// Actor message protocol (spec: actor-message-protocol; design D6/D7).
///
/// Every message owns its payload: crossing a queue is a copy or a move.
/// There are exactly two data-plane message types (`midi_received`,
/// `midi_to_wire`) on the data lane, and a small control-plane variant on
/// the control lane. Data-plane messages carry MIDI inline (no allocation
/// within capacity); oversized payloads use a bounded heap escape pool.

#pragma once

#include "rtpmidid/logger.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

namespace rtpmididns {

// The mailbox joins two lanes of the types below; the template is declared
// here and defined in mailbox.hpp (control messages carry shared_ptr
// handles to it, so it must be at least declared at this point).
template <typename DataT, typename ControlT> class mailbox_t;

struct data_message_t;
struct control_message_t;

using actor_mailbox_t = mailbox_t<data_message_t, control_message_t>;
/// Mailbox handle for control-plane use (design D6): `shared_ptr`, so
/// posting to a terminated actor's mailbox stays safe.
using mailbox_handle_t = std::shared_ptr<actor_mailbox_t>;

using peer_id_t = uint32_t;
using actor_id_t = uint32_t;

/// Request/response envelope (design D7): correlation id + optional reply
/// mailbox handle for requester-driven flows.
struct hdr_t {
  uint64_t corr = 0;
};

/**
 * Requester-side deadline helper (design D7/D15): tracks pending requests
 * keyed by correlation id with requester-side deadlines. Backs the router's
 * pending-table dispatch (busy actors that cannot park a selective wait).
 * Deadlines are enforced exclusively by the requester; responders never
 * know about them. Responses with unknown or expired correlation ids are
 * silently discarded (`is_pending` returns false).
 */
class pending_request_table_t {
public:
  void add(uint64_t corr, std::chrono::milliseconds timeout) {
    entries_[corr] = std::chrono::steady_clock::now() + timeout;
  }

  /// True while the request is live (present and not past its deadline).
  bool is_pending(uint64_t corr) const {
    auto it = entries_.find(corr);
    if (it == entries_.end()) {
      return false;
    }
    if (std::chrono::steady_clock::now() >= it->second) {
      return false; // expired: treated as unknown, silently discarded
    }
    return true;
  }

  void erase(uint64_t corr) { entries_.erase(corr); }
  /// Drop expired entries (housekeeping; lookups already handle expiry).
  void expire() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(entries_, [&](const auto &kv) { return now >= kv.second; });
  }
  size_t size() const { return entries_.size(); }

private:
  std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> entries_;
};

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

// ---------------------------------------------------------------------------
// Control-plane catalog (design D7/D9; extended per capability as the
// router, peers, worker and control socket are implemented).
// ---------------------------------------------------------------------------

/// Graceful stop: processed by the actor loop, runs stop hooks, exits.
struct stop_t {
  hdr_t hdr;
};
/// Posted by the actor wrapper after the loop exited (join is safe).
struct stopped_t {
  hdr_t hdr;
};
/// Fatal error: posted to the supervisor mailbox; loop exits.
struct actor_died_t {
  actor_id_t id = 0;
  std::string reason;
};
/// Router -> supervisor: delegate joining a wedged thread to the reaper.
struct reap_actor_t {
  std::jthread thread;
  std::string reason;
};
/// Topology change notification (router -> subscribers/partners).
enum class peer_event_kind_t : uint8_t { registered, removed, stopped, died };
struct peer_event_t {
  peer_event_kind_t kind = peer_event_kind_t::registered;
  peer_id_t peer_id = 0;
};
/// Generic typed-content carrier (notices, event streams, test messages).
struct control_payload_t {
  uint32_t tag = 0;
  std::string text;
};

/// Control-plane message: a small variant; never pays for inline MIDI
/// storage (design D6). Move/copy-only, self-owning.
struct control_message_t {
  std::variant<std::monostate, stop_t, stopped_t, actor_died_t, reap_actor_t,
               peer_event_t, control_payload_t>
      v;

  control_message_t() = default;
  template <typename T> control_message_t(T value) : v(std::move(value)) {}
};

// helpers to build typed control messages concisely
inline control_message_t make_stop(hdr_t hdr = {}) {
  return control_message_t(stop_t{hdr});
}
inline control_message_t make_stopped(hdr_t hdr = {}) {
  return control_message_t(stopped_t{hdr});
}
inline control_message_t make_actor_died(actor_id_t id, std::string reason) {
  return control_message_t(actor_died_t{id, std::move(reason)});
}
inline control_message_t make_peer_event(peer_event_kind_t kind,
                                         peer_id_t peer_id) {
  return control_message_t(peer_event_t{kind, peer_id});
}
inline control_message_t make_control_payload(uint32_t tag,
                                              std::string text = {}) {
  return control_message_t(control_payload_t{tag, std::move(text)});
}

} // namespace rtpmididns
