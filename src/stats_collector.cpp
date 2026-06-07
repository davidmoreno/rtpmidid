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
#include "stats_collector.hpp"
#include "event_subscription.hpp"
#include <rtpmidid/dm_json/runtime.hpp>
#include <rtpmidid/logger.hpp>

namespace rtpmididns {

stats_collector_t::~stats_collector_t() { stop(); }

// ── Lifecycle ───────────────────────────────────────────────────────────────

void stats_collector_t::start() {
  if (running_.exchange(true))
    return;
  thread_ = std::thread(&stats_collector_t::thread_loop, this);
}

void stats_collector_t::stop() {
  if (!running_.exchange(false))
    return;
  if (thread_.joinable())
    thread_.join();
}

// ── Peer registry ──────────────────────────────────────────────────────────

void stats_collector_t::register_peer(peer_id_t peer_id) {
  // Allocations happen here (collector setup / router thread before start),
  // never in the hot MIDI path.
  std::unique_lock<std::shared_mutex> lock(counters_mutex_);
  counters_.try_emplace(peer_id);
}

void stats_collector_t::unregister_peer(peer_id_t peer_id) {
  std::unique_lock<std::shared_mutex> lock(counters_mutex_);
  counters_.erase(peer_id);
  // dirty_ is collector-thread-only, no lock needed
}

// ── Hot-path notifications ─────────────────────────────────────────────────

void stats_collector_t::notify_sent(peer_id_t peer_id) {
  queue_.enqueue({peer_id, true});
}

void stats_collector_t::notify_recv(peer_id_t peer_id) {
  queue_.enqueue({peer_id, false});
}

// ── Lock-free-ish reads ────────────────────────────────────────────────────

uint64_t stats_collector_t::get_sent(peer_id_t peer_id) const {
  std::shared_lock<std::shared_mutex> lock(counters_mutex_);
  auto it = counters_.find(peer_id);
  if (it == counters_.end())
    return 0;
  return it->second.sent.load(std::memory_order_relaxed);
}

uint64_t stats_collector_t::get_recv(peer_id_t peer_id) const {
  std::shared_lock<std::shared_mutex> lock(counters_mutex_);
  auto it = counters_.find(peer_id);
  if (it == counters_.end())
    return 0;
  return it->second.recv.load(std::memory_order_relaxed);
}

// ── Subscriber registry ────────────────────────────────────────────────────

void stats_collector_t::register_subscriber(
    std::weak_ptr<event_subscription_manager_t> subs) {
  std::lock_guard<std::mutex> lock(subs_mutex_);
  subscribers_.push_back(std::move(subs));
}

std::function<void(peer_id_t)> stats_collector_t::sent_callback() {
  return [this](peer_id_t pid) { notify_sent(pid); };
}

std::function<void(peer_id_t)> stats_collector_t::recv_callback() {
  return [this](peer_id_t pid) { notify_recv(pid); };
}

// ── Collector thread ───────────────────────────────────────────────────────

void stats_collector_t::thread_loop() {
  while (running_.load(std::memory_order_acquire)) {
    // ── Drain the SPSC queue ────────────────────────────────────────
    stats_msg_t msg;
    while (queue_.dequeue(msg)) {
      // Increment the cache-aligned atomic counter for this peer.
      // The collector thread is the only writer; readers use shared_lock.
      {
        std::shared_lock<std::shared_mutex> lock(counters_mutex_);
        auto it = counters_.find(msg.peer_id);
        if (it != counters_.end()) {
          if (msg.is_sent)
            it->second.sent.fetch_add(1, std::memory_order_relaxed);
          else
            it->second.recv.fetch_add(1, std::memory_order_relaxed);
        }
      }
      // Mark dirty (collector-thread-only, no lock needed)
      dirty_[msg.peer_id] = true;
    }

    // ── Flush dirty peers to subscribers ─────────────────────────────
    if (!dirty_.empty()) {
      std::vector<std::shared_ptr<event_subscription_manager_t>> active;
      {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        auto it = subscribers_.begin();
        while (it != subscribers_.end()) {
          auto s = it->lock();
          if (s) {
            active.push_back(std::move(s));
            ++it;
          } else {
            it = subscribers_.erase(it);
          }
        }
      }

      if (!active.empty()) {
        std::shared_lock<std::shared_mutex> lock(counters_mutex_);
        for (auto &kv : dirty_) {
          if (!kv.second)
            continue;
          kv.second = false;

          const peer_id_t pid = kv.first;
          auto cit = counters_.find(pid);
          if (cit == counters_.end())
            continue;

          const auto sent =
              cit->second.sent.load(std::memory_order_relaxed);
          const auto recv =
              cit->second.recv.load(std::memory_order_relaxed);

          // Lightweight JSON: {"id":N, "stats":{"recv":R, "sent":S}}
          dmjson::writer_t w;
          w.begin_object();
          w.key("id");
          w.int_value(static_cast<int64_t>(pid));
          w.key("stats");
          w.begin_object();
          w.key("recv");
          w.int_value(static_cast<int64_t>(recv));
          w.key("sent");
          w.int_value(static_cast<int64_t>(sent));
          w.end_object();
          w.end_object();
          std::string json;
          w.swap_into_string(json);

          for (auto &subs : active)
            subs->emit("router.peer_updated", json);
        }
      }
    }

    // ── Sleep until next flush ────────────────────────────────────────
    std::this_thread::sleep_for(kFlushInterval);
  }
}

} // namespace rtpmididns
