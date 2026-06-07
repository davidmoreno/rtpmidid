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
#pragma once

#include "midipeer.hpp"
#include <rtpmidid/lockfree_queue.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

class event_subscription_manager_t;

/**
 * @short Lock-free statistics collector — owns all peer counters.
 *
 * The router MIDI path (producer) pushes {peer_id, direction} through a
 * bounded SPSC lockfree_queue whenever a peer sends or receives a packet.
 * The collector thread drains the queue in batches every ~200 ms,
 * increments its own cache-aligned atomic counters, and emits
 * `router.peer_updated` events to registered subscribers.
 *
 * Counter storage is a mutex-protected unordered_map indexed by peer_id.
 * Allocations happen only in the collector thread (register/unregister
 * peer) and the subscriber registry — never in the router hot path.
 *
 * Mechanical-sympathy properties:
 *  - Hot path (router MIDI): single SPSC enqueue, lock-free, no allocs,
 *    no cache-line bouncing with collector thread
 *  - Per-peer counters: alignas(64) → each peer isolated on its own
 *    cache line within the map; router and collector never touch the
 *    same line
 *  - Collector thread: all allocations (map insert/erase, JSON building)
 *    happen here, outside the hot path
 *  - Reads (status_rows): shared_lock + map find → atomic load, no
 *    contention with writer (collector thread)
 */
class stats_collector_t {
public:
  stats_collector_t() = default;
  ~stats_collector_t();

  stats_collector_t(const stats_collector_t &) = delete;
  stats_collector_t &operator=(const stats_collector_t &) = delete;
  stats_collector_t(stats_collector_t &&) = delete;
  stats_collector_t &operator=(stats_collector_t &&) = delete;

  // ── Lifecycle ───────────────────────────────────────────────────────

  void start();
  void stop();

  // ── Peer registry (called from router add_peer / remove_peer) ───────

  void register_peer(peer_id_t peer_id);
  void unregister_peer(peer_id_t peer_id);

  // ── Hot-path notifications (lock-free SPSC enqueue) ─────────────────

  void notify_sent(peer_id_t peer_id);
  void notify_recv(peer_id_t peer_id);

  // ── Lock-free-ish reads (shared_lock, called from status_rows) ──────

  uint64_t get_sent(peer_id_t peer_id) const;
  uint64_t get_recv(peer_id_t peer_id) const;

  // ── Event subscriber registry ───────────────────────────────────────

  void
  register_subscriber(std::weak_ptr<event_subscription_manager_t> subs);

  /** Convenience: returns callables for the router hot-path callbacks. */
  std::function<void(peer_id_t)> sent_callback();
  std::function<void(peer_id_t)> recv_callback();

private:
  void thread_loop();

  // ── Cache-aligned per-peer counters ─────────────────────────────────
  struct alignas(64) peer_counters_t {
    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> recv{0};
  };
  static_assert(sizeof(peer_counters_t) <= 64,
                "peer_counters_t must fit in one cache line");

  mutable std::shared_mutex counters_mutex_;
  std::unordered_map<peer_id_t, peer_counters_t> counters_;

  // ── Dirty tracking (collector-thread-only) ──────────────────────────
  std::unordered_map<peer_id_t, bool> dirty_;

  // ── SPSC queue ──────────────────────────────────────────────────────
  static constexpr size_t kQueueSize = 4096;
  struct stats_msg_t {
    peer_id_t peer_id;
    bool is_sent; // true = sent, false = recv
  };
  rtpmidid::lockfree_queue<stats_msg_t, kQueueSize> queue_;

  // ── Collector thread ────────────────────────────────────────────────
  std::thread thread_;
  std::atomic<bool> running_{false};

  // ── Subscriber registry ─────────────────────────────────────────────
  std::mutex subs_mutex_;
  std::vector<std::weak_ptr<event_subscription_manager_t>> subscribers_;

  static constexpr auto kFlushInterval = std::chrono::milliseconds(200);
};

} // namespace rtpmididns
