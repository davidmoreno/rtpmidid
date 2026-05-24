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

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rtpmidid {

/**
 * @short Tagged response value for an actor request.
 *
 * `id` matches the request id assigned by `reply_channel_t::next_id()`.
 * Either `value` or `error` is populated; `value` carries the typed payload as
 * `std::any` (cast on the receiving side).
 */
struct reply_envelope_t {
  uint64_t id{0};
  std::any value;
  std::string error;
};

/**
 * @short Per-call reply queue with id-based wait.
 *
 * Designed to pair with the actor priority queue model: the caller allocates a
 * `reply_channel_t`, generates an id via `next_id()`, includes both in the
 * request, and then `wait(id, timeout)` blocks until the matching reply
 * arrives. Replies for other ids stay in the channel for later waiters; they
 * do not disturb the main actor queue, which keeps draining requests on its
 * own thread.
 *
 * Thread safety: producer (`post`) and consumer (`wait`) may be different
 * threads. Multiple threads may post or wait concurrently.
 */
class reply_channel_t {
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<reply_envelope_t> envelopes_;
  std::atomic<uint64_t> next_id_{1};
  bool closed_{false};

public:
  reply_channel_t() = default;
  ~reply_channel_t() = default;

  reply_channel_t(const reply_channel_t &) = delete;
  reply_channel_t &operator=(const reply_channel_t &) = delete;
  reply_channel_t(reply_channel_t &&) = delete;
  reply_channel_t &operator=(reply_channel_t &&) = delete;

  uint64_t next_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
  }

  /** Producer-side: enqueue a response. Wakes any waiter. */
  void post(reply_envelope_t env) {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      envelopes_.push_back(std::move(env));
    }
    cv_.notify_all();
  }

  /**
   * Block until an envelope with `id` arrives, or `timeout` elapses.
   *
   * Other envelopes already in the channel (with different ids) are left
   * untouched for future waiters. On timeout returns an envelope with
   * `error == "timeout"` and `value` empty.
   *
   * If the channel has been `close()`d before the matching reply arrives,
   * returns an envelope with `error == "closed"`.
   */
  reply_envelope_t wait(uint64_t id, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
      auto it =
          std::find_if(envelopes_.begin(), envelopes_.end(),
                       [id](const reply_envelope_t &e) { return e.id == id; });
      if (it != envelopes_.end()) {
        reply_envelope_t out = std::move(*it);
        envelopes_.erase(it);
        return out;
      }
      if (closed_) {
        reply_envelope_t out;
        out.id = id;
        out.error = "closed";
        return out;
      }
      if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
        reply_envelope_t out;
        out.id = id;
        out.error = "timeout";
        return out;
      }
    }
  }

  /**
   * Wake all pending waiters. After `close()`, `wait()` returns immediately
   * with `error == "closed"` (unless a matching reply is already queued).
   */
  void close() {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return envelopes_.empty();
  }
};

/**
 * @short Reply slot embedded in actor requests.
 *
 * `channel == nullptr` ⇒ fire-and-forget request.
 */
struct reply_slot_t {
  std::shared_ptr<reply_channel_t> channel{};
  uint64_t id{0};

  bool wants_reply() const noexcept { return static_cast<bool>(channel); }
};

} // namespace rtpmidid
