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
#include "dm_json_status.hpp"
#include "midipeer.hpp"
#include "router_command.hpp"
#include "rtpmidid/blocking_priority_queue.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/reply_channel.hpp"
#include "rtpmidid/signal.hpp"
#include "rtpmidid/utils.hpp"
#include <any>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rtpmididns {
class mididata_t;
class midipeer_t;

struct peerconnection_t {
  uint32_t id = 0;
  std::shared_ptr<midipeer_t> peer;
  std::vector<peer_id_t> send_to{};
};

/**
 * @short Actor-style MIDI router.
 *
 * Every public method that mutates or reads router state is dispatched onto
 * the router thread through a three-level priority queue (HIGH = MIDI,
 * NORMAL = topology / signals, LOW = read snapshots). The router thread is
 * the sole owner of `peers_` and friends, so no mutex is required.
 *
 * Messages are typed (`router_command_t` is a `std::variant` of per-purpose
 * structs) and dispatched via `std::visit`. Reads return values via a
 * per-call `reply_channel_t` keyed by id, so the router thread can keep
 * processing other requests while a caller is blocked. When the calling
 * thread already is the router thread (e.g. from a signal listener) or the
 * router thread is not running (tests), the call short-circuits to inline
 * execution.
 */
class midirouter_t : public std::enable_shared_from_this<midirouter_t> {
  NON_COPYABLE_NOR_MOVABLE(midirouter_t)

public:
  midirouter_t();
  ~midirouter_t();

  void start_router_thread();
  void stop_router_thread();

  bool is_running() const { return router_running_.load(); }

  // --- Mutating API (signatures preserved) ---

  peer_id_t add_peer(std::shared_ptr<midipeer_t> peer);
  void remove_peer(peer_id_t peer_id);
  void connect(peer_id_t from, peer_id_t to);
  void disconnect(peer_id_t from, peer_id_t to);

  /** Send MIDI from `from` to all connected destinations. HIGH priority. */
  void send_midi(peer_id_t from, const mididata_t &data);
  /** Send MIDI from `from` to a single peer `to`. HIGH priority. */
  void send_midi(peer_id_t from, peer_id_t to, const mididata_t &data);

  void event(peer_id_t from, peer_id_t to, midipeer_event_e evt);
  void event(peer_id_t from, midipeer_event_e evt);

  void remove_all_peers();
  /** Force-clear peers (alias for remove_all_peers). */
  void clear();

  // --- Read API (signatures preserved) ---

  std::shared_ptr<midipeer_t> get_peer_by_id(peer_id_t peer_id);
  size_t peer_count() const;
  std::vector<peer_id_t> peer_ids() const;
  std::vector<peer_id_t> send_targets_for(peer_id_t from) const;
  std::vector<router_peer_row_t> status_rows() const;

  void peer_connection_loop(peer_id_t peer_id,
                            std::function<void(std::shared_ptr<midipeer_t>)>);

  /** Iterate peers (downcast to T). Lambda runs on the router thread. */
  template <typename T = midipeer_t>
  void for_each_peer(const std::function<void(T *)> &f) {
    dispatch_for_each_peer([&f](midirouter_t &router) {
      for (auto &kv : router.peers_) {
        auto t = dynamic_cast<T *>(kv.second.peer.get());
        if (t)
          f(t);
      }
    });
  }

  // --- Backwards-compatible enqueue_* aliases (always go through queue) ---

  bool enqueue_send_midi(peer_id_t from, const mididata_t &data);
  bool enqueue_send_midi(peer_id_t from, peer_id_t to, const mididata_t &data);
  bool enqueue_connect(peer_id_t from, peer_id_t to);
  bool enqueue_disconnect(peer_id_t from, peer_id_t to);
  bool enqueue_remove_peer(peer_id_t peer_id);
  bool enqueue_event(peer_id_t from, peer_id_t to, midipeer_event_e evt);
  bool enqueue_event(peer_id_t from, midipeer_event_e evt);

  // --- Test / shutdown helpers ---

  /** Drain the queue once on the calling thread (for deterministic tests). */
  void drain_for_tests();

  // --- Signals (fired on the router thread via typed signal_*_t messages) ---

  rtpmidid::signal_t<peer_id_t, peer_id_t> connected_event;
  rtpmidid::signal_t<peer_id_t, peer_id_t> disconnected_event;
  rtpmidid::signal_t<peer_id_t> peer_added_event;
  rtpmidid::signal_t<peer_id_t, midipeer_event_e> peer_event;

private:
  // --- Router-thread-owned state ---

  peer_id_t max_id_{1};
  std::unordered_map<peer_id_t, peerconnection_t> peers_;
  std::set<peer_id_t> removing_peers_;

  // --- Threading / queue ---

  rtpmidid::blocking_priority_queue<router_command_t,
                                    /* HighSize  */ 4096,
                                    /* NormalSize*/ 256,
                                    /* LowSize   */ 64>
      queue_;

  std::thread router_thread_;
  std::atomic<bool> router_running_{false};

  // --- Internal dispatch (one per variant alternative) ---

  void router_thread_loop();

  void handle(router_cmd::send_midi_t &cmd);

  void handle(router_cmd::add_peer_t &cmd);
  void handle(router_cmd::remove_peer_t &cmd);
  void handle(router_cmd::remove_all_peers_t &cmd);
  void handle(router_cmd::connect_t &cmd);
  void handle(router_cmd::disconnect_t &cmd);
  void handle(router_cmd::event_directed_t &cmd);
  void handle(router_cmd::event_broadcast_t &cmd);

  void handle(router_cmd::signal_peer_added_t &cmd);
  void handle(router_cmd::signal_connected_t &cmd);
  void handle(router_cmd::signal_disconnected_t &cmd);
  void handle(router_cmd::signal_peer_event_t &cmd);

  void handle(router_cmd::query_peer_count_t &cmd);
  void handle(router_cmd::query_peer_ids_t &cmd);
  void handle(router_cmd::query_send_targets_t &cmd);
  void handle(router_cmd::query_get_peer_t &cmd);
  void handle(router_cmd::query_status_rows_t &cmd);

  void handle(router_cmd::for_each_peer_t &cmd);
  void handle(router_cmd::peer_connection_loop_t &cmd);

  void handle(router_cmd::shutdown_t &cmd);

  // --- Inline implementations (run on router thread, no locks) ---

  peer_id_t add_peer_impl(std::shared_ptr<midipeer_t> peer);
  void remove_peer_impl(peer_id_t id);
  void remove_all_peers_impl();
  void connect_impl(peer_id_t from, peer_id_t to);
  void disconnect_impl(peer_id_t from, peer_id_t to);
  void send_midi_inline(peer_id_t from, peer_id_t to, const uint8_t *data,
                        size_t size);
  void event_directed_impl(peer_id_t from, peer_id_t to, midipeer_event_e evt);
  void event_broadcast_impl(peer_id_t from, midipeer_event_e evt);
  std::vector<router_peer_row_t> status_rows_impl();

  // --- Helpers ---

  bool on_router_thread() const;
  bool sync_mode() const;
  bool enqueue(router_command_t &&cmd, rtpmidid::queue_priority_e prio);

  /** Post a typed signal message; in sync mode fires the signal directly. */
  void post_signal_peer_added(peer_id_t peer_id);
  void post_signal_connected(peer_id_t from, peer_id_t to);
  void post_signal_disconnected(peer_id_t from, peer_id_t to);
  void post_signal_peer_event(peer_id_t peer_id, midipeer_event_e evt);

  /**
   * Dispatch a typed query.
   *  - `proto` is the typed command struct (its `reply` field will be filled
   *    in here). Move-only.
   *  - `sync_fn` is the inline computation used when called on the router
   *    thread or in sync mode.
   */
  template <typename T, typename Cmd>
  T dispatch_query(Cmd proto, rtpmidid::queue_priority_e prio,
                   std::function<T(midirouter_t &)> sync_fn) const;

  /** Dispatch a `for_each_peer_t` (named iterator wrapper). */
  void dispatch_for_each_peer(std::function<void(midirouter_t &)> task);

  static constexpr std::chrono::milliseconds kReplyTimeout{5000};
  /** Per-peer latency-query budget shared across `status_rows_impl`. */
  static constexpr std::chrono::milliseconds kStatusLatencyBudget{500};
};

template <typename T, typename Cmd>
T midirouter_t::dispatch_query(Cmd proto, rtpmidid::queue_priority_e prio,
                               std::function<T(midirouter_t &)> sync_fn) const {
  if (sync_mode() || on_router_thread()) {
    return sync_fn(const_cast<midirouter_t &>(*this));
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  proto.reply = rtpmidid::reply_slot_t{channel, id};
  if (!const_cast<midirouter_t *>(this)->enqueue(
          router_command_t{std::move(proto)}, prio)) {
    return T{};
  }
  auto env = channel->wait(id, kReplyTimeout);
  if (!env.error.empty()) {
    return T{};
  }
  try {
    return std::any_cast<T>(env.value);
  } catch (const std::bad_any_cast &) {
    return T{};
  }
}

} // namespace rtpmididns
