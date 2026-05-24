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
#include "peer_command.hpp"
#include "rtpmidid/formatterhelper.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/priority_mpsc_queue.hpp"
#include "rtpmidid/reply_channel.hpp"
#include "rtpmidid/stats.hpp"
#include "rtpmidid/threading_types.hpp"
#include "rtpmidid/utils.hpp"
#include <rtpmidid/dm_json/runtime.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace rtpmididns {

using midipeer_id_t = uint32_t;
constexpr midipeer_id_t MIDIPEER_ID_INVALID =
    std::numeric_limits<uint32_t>::max();

/** Router-level peer identifier; alias of `midipeer_id_t`. */
using peer_id_t = midipeer_id_t;

class mididata_t;
class midirouter_t;

enum midipeer_event_e {
  CONNECTED_ROUTER = 1,
  DISCONNECTED_ROUTER,
  CONNECTED_PEER,
  DISCONNECTED_PEER,
};

} // namespace rtpmididns

ENUM_FORMATTER_BEGIN(rtpmididns::midipeer_event_e);
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::CONNECTED_ROUTER,
                       "CONNECTED_ROUTER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::DISCONNECTED_ROUTER,

                       "DISCONNECTED_ROUTER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::CONNECTED_PEER,
                       "CONNECTED_PEER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::DISCONNECTED_PEER,
                       "DISCONNECTED_PEER");
ENUM_FORMATTER_DEFAULT();
ENUM_FORMATTER_END();

namespace rtpmididns {

/**
 * @short Actor-style MIDI peer.
 *
 * Each peer owns a single thread that drains a three-priority queue:
 *  - HIGH:   inbound MIDI packets (`peer_cmd::process_midi_t`).
 *  - NORMAL: control tasks (`peer_cmd::run_task_t`, `peer_cmd::shutdown_t`).
 *  - LOW:    state queries (`peer_cmd::query_t`, e.g. latency snapshot).
 *
 * The thread is the sole owner of mutable peer state, so `internal_*_stats_`
 * is touched without a mutex. Reads from other threads enqueue a `query_t`
 * and wait on a `reply_channel_t`. When the peer thread is not running
 * (tests / setup) the call short-circuits to direct execution.
 */
class midipeer_t : public std::enable_shared_from_this<midipeer_t> {
  NON_COPYABLE_NOR_MOVABLE(midipeer_t);

protected:
  std::thread peer_thread_;
  std::atomic<bool> thread_running_{false};
  mutable std::mutex thread_mutex_;
  mutable std::condition_variable thread_wakeup_;

  static constexpr size_t HIGH_QUEUE_SIZE = 1024;
  static constexpr size_t NORMAL_QUEUE_SIZE = 64;
  static constexpr size_t LOW_QUEUE_SIZE = 16;

  mutable rtpmidid::priority_mpsc_queue<peer_command_t, HIGH_QUEUE_SIZE,
                                        NORMAL_QUEUE_SIZE, LOW_QUEUE_SIZE>
      peer_queue_;

  void peer_thread_loop();
  virtual void process_midi_packet(const rtpmidid::midi_packet_t &packet);
  void enqueue_to_router(const mididata_t &data);

  // --- Internal latency stats (peer-thread-owned, no mutex) ---
  rtpmidid::stats_t internal_until_send_stats_{20, std::chrono::seconds(120)};
  rtpmidid::stats_t internal_send_midi_stats_{20, std::chrono::seconds(120)};
  std::atomic<int64_t> internal_last_until_send_ns_{0};
  std::atomic<int64_t> internal_last_send_midi_ns_{0};

  /** Inline read of latency stats (only safe on the peer thread or in sync mode). */
  internal_latency_ms_t internal_latency_stats_impl() const;

  // --- Variant dispatch (peer thread) ---
  void handle(peer_cmd::process_midi_t &cmd);
  void handle(peer_cmd::send_to_router_t &cmd);
  void handle(peer_cmd::run_task_t &cmd);
  void handle(peer_cmd::query_t &cmd);
  void handle(peer_cmd::shutdown_t &cmd);

  bool on_peer_thread() const;
  bool peer_sync_mode() const { return !thread_running_.load(); }

  /** Submit a typed read to the peer thread and block on the reply. */
  template <typename T>
  T submit_peer_query(std::function<T(midipeer_t &)> q,
                      rtpmidid::queue_priority_e prio =
                          rtpmidid::queue_priority_e::LOW) const;

public:
  std::shared_ptr<midirouter_t> router;
  midipeer_id_t peer_id = 0;
  std::atomic<int> packets_sent{0};
  std::atomic<int> packets_recv{0};

  midipeer_t() = default;
  virtual ~midipeer_t();

  void start_thread();
  void stop_thread();

  /** Latest latency snapshot. Goes through the peer queue when not on the peer thread. */
  internal_latency_ms_t internal_latency_stats() const;

  /**
   * Queue an inbound MIDI packet (HIGH priority). Producer: typically the
   * router thread when this peer is a destination of `send_midi`.
   */
  bool enqueue_midi_packet(const rtpmidid::midi_packet_t &packet);

  /** Fill @a row with this peer's public status fields (router adds id, type, send_to, stats, latency). */
  virtual router_peer_row_t status() const = 0;

  virtual void send_midi(midipeer_id_t from, const mididata_t &) = 0;
  virtual void event(midipeer_event_e event, midipeer_id_t from) {
    DEBUG("Peer event={} from={}", event, from);
  };

  /** Control command from `peerId.cmd`; writes JSON value for `result` into @a out. */
  virtual bool control_peer_command(std::string_view cmd,
                                    std::string_view params_json,
                                    ::rtpmididns::dmjson::writer_t &out,
                                    std::string &out_error);

  virtual const char *get_type() const = 0;

  /** Called from midirouter_t::add_peer after peer_id and router are assigned. */
  virtual void on_router_attached() {}
};

template <typename T>
T midipeer_t::submit_peer_query(std::function<T(midipeer_t &)> q,
                                rtpmidid::queue_priority_e prio) const {
  if (peer_sync_mode() || on_peer_thread()) {
    return q(const_cast<midipeer_t &>(*this));
  }
  auto channel = std::make_shared<rtpmidid::reply_channel_t>();
  const uint64_t id = channel->next_id();
  peer_cmd::query_t cmd;
  cmd.query = [q = std::move(q)](midipeer_t &p) -> std::any {
    return std::any(q(p));
  };
  cmd.reply = rtpmidid::reply_slot_t{channel, id};
  if (!peer_queue_.enqueue(peer_command_t{std::move(cmd)}, prio)) {
    return T{};
  }
  thread_wakeup_.notify_one();
  auto env = channel->wait(id, std::chrono::milliseconds(2000));
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

const char *format_as(rtpmididns::midipeer_event_e event);
