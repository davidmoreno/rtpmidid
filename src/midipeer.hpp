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

#include "dm_json_status.hpp"
#include "rtpmidid/formatterhelper.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/utils.hpp"
#include "rtpmidid/lockfree_queue.hpp"
#include "rtpmidid/stats.hpp"
#include "rtpmidid/threading_types.hpp"
#include <rtpmidid/dm_json/runtime.hpp>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>

namespace rtpmidid {
struct midi_packet_t;
struct routing_request_t;
struct peer_command_t;
}

namespace rtpmididns {

using midipeer_id_t = uint32_t;
constexpr midipeer_id_t MIDIPEER_ID_INVALID =
    std::numeric_limits<uint32_t>::max();

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
 * @short Any peer that can read and write midi
 */
class midipeer_t : public std::enable_shared_from_this<midipeer_t> {
  NON_COPYABLE_NOR_MOVABLE(midipeer_t);

protected:
  std::thread peer_thread;
  std::atomic<bool> thread_running{false};
  std::mutex thread_mutex;
  std::condition_variable thread_wakeup;

  static constexpr size_t INPUT_QUEUE_SIZE = 1024;
  static constexpr size_t OUTPUT_QUEUE_SIZE = 256;
  static constexpr size_t COMMAND_QUEUE_SIZE = 32;

  rtpmidid::lockfree_queue<rtpmidid::midi_packet_t, INPUT_QUEUE_SIZE> input_queue;
  rtpmidid::lockfree_queue<rtpmidid::routing_request_t, OUTPUT_QUEUE_SIZE> output_queue;
  rtpmidid::lockfree_queue<rtpmidid::peer_command_t, COMMAND_QUEUE_SIZE> command_queue;

  void peer_thread_loop();
  virtual void process_midi_packet(const rtpmidid::midi_packet_t &packet);
  void enqueue_to_router(const mididata_t &data);

  mutable std::mutex internal_latency_mutex_;
  rtpmidid::stats_t internal_until_send_stats_{20, std::chrono::seconds(120)};
  rtpmidid::stats_t internal_send_midi_stats_{20, std::chrono::seconds(120)};
  std::atomic<int64_t> internal_last_until_send_ns_{0};
  std::atomic<int64_t> internal_last_send_midi_ns_{0};

public:
  internal_latency_ms_t internal_latency_stats() const;
  std::shared_ptr<midirouter_t> router;
  midipeer_id_t peer_id = 0;
  std::atomic<int> packets_sent{0};
  std::atomic<int> packets_recv{0};

  midipeer_t() = default;
  virtual ~midipeer_t();

  void start_thread();
  void stop_thread();

  bool enqueue_midi_packet(const rtpmidid::midi_packet_t &packet);
  bool enqueue_command(const rtpmidid::peer_command_t &cmd);

  /** Fill @a row with this peer's public status fields (router adds id, type, send_to, stats, latency). */
  virtual router_peer_row_t status() const = 0;

  virtual void send_midi(midipeer_id_t from, const mididata_t &) = 0;
  virtual void event(midipeer_event_e event, midipeer_id_t from) {
    DEBUG("Peer event={} from={}", event, from);
  };

  /** Control command from `peerId.cmd`; writes JSON value for `result` into @a out. */
  virtual bool control_peer_command(std::string_view cmd, std::string_view params_json,
                                    ::rtpmididns::dmjson::writer_t &out,
                                    std::string &out_error);

  virtual const char *get_type() const = 0;

  /** Called from midirouter_t::add_peer after peer_id and router are assigned. */
  virtual void on_router_attached() {}
};
} // namespace rtpmididns

const char *format_as(rtpmididns::midipeer_event_e event);
