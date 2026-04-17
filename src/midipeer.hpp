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

#include "json_fwd.hpp"
#include "rtpmidid/formatterhelper.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/utils.hpp"
#include "rtpmidid/lockfree_queue.hpp"
#include "rtpmidid/stats.hpp"
#include "rtpmidid/threading_types.hpp"
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

// Forward declaration
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
 *
 * Must be inherited by the real clients
 */
class midipeer_t : public std::enable_shared_from_this<midipeer_t> {
  NON_COPYABLE_NOR_MOVABLE(midipeer_t);

protected:
  // Thread management
  std::thread peer_thread;
  std::atomic<bool> thread_running{false};
  std::mutex thread_mutex;
  std::condition_variable thread_wakeup;
  
  // Queues for thread communication
  static constexpr size_t INPUT_QUEUE_SIZE = 1024;
  static constexpr size_t OUTPUT_QUEUE_SIZE = 256;
  static constexpr size_t COMMAND_QUEUE_SIZE = 32;
  
  rtpmidid::lockfree_queue<rtpmidid::midi_packet_t, INPUT_QUEUE_SIZE> input_queue;
  rtpmidid::lockfree_queue<rtpmidid::routing_request_t, OUTPUT_QUEUE_SIZE> output_queue;
  rtpmidid::lockfree_queue<rtpmidid::peer_command_t, COMMAND_QUEUE_SIZE> command_queue;
  
  // Peer thread main loop
  void peer_thread_loop();
  
  // Process incoming MIDI (called from peer thread)
  virtual void process_midi_packet(const rtpmidid::midi_packet_t &packet);
  
  // Enqueue MIDI to router (thread-safe)
  void enqueue_to_router(const mididata_t &data);

  mutable std::mutex internal_latency_mutex_;
  rtpmidid::stats_t internal_until_send_stats_{20, std::chrono::seconds(120)};
  rtpmidid::stats_t internal_send_midi_stats_{20, std::chrono::seconds(120)};
  std::atomic<int64_t> internal_last_until_send_ns_{0};
  std::atomic<int64_t> internal_last_send_midi_ns_{0};

public:
  /** Time from peer input enqueue to start of `send_midi`, and `send_midi` duration (ms). */
  json_t internal_latency_stats_json() const;
  std::shared_ptr<midirouter_t> router;
  midipeer_id_t peer_id = 0;
  /// @brief statistics
  std::atomic<int> packets_sent{0};
  /// @brief statistics
  std::atomic<int> packets_recv{0};

  midipeer_t() = default;
  virtual ~midipeer_t();
  
  // Thread management
  void start_thread();
  void stop_thread();
  
  // Thread-safe enqueue methods (can be called from any thread)
  bool enqueue_midi_packet(const rtpmidid::midi_packet_t &packet);
  bool enqueue_command(const rtpmidid::peer_command_t &cmd);

  /**
   *  @brief Returns the status of the
   *
   * Basic data can be get with utils::peer_status
   *
   * @return  json_t
   */
  virtual json_t status() = 0;
  /**
   * @brief Send a midi message to the peer
   *
   * @param from The peer that sends the message
   * @param data The midi message
   */
  virtual void send_midi(midipeer_id_t from, const mididata_t &) = 0;
  /**
   * @brief Called when the peer is connected
   *
   * Normally do nothing, but might need to open a file and close
   * when all disconnect signas are received
   */
  virtual void event(midipeer_event_e event, midipeer_id_t from) {
    DEBUG("Peer event={} from={}", event, from);
  };
  /**
   * @brief Command as sent by the control interface
   *
   * @param cmd The command
   * @param data The data
   * @return json_t The response
   */
  virtual json_t command(const std::string &cmd, const json_t &data);
  /**
   * @brief Get the type of the peer
   */
  virtual const char *get_type() const = 0;
};
} // namespace rtpmididns

const char *format_as(rtpmididns::midipeer_event_e event);