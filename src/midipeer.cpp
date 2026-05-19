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

#include "midipeer.hpp"
#include "midirouter.hpp"
#include "mididata.hpp"
#include "dm_json_generated.hpp"
#include "rtpmidid/shutdown_signals.hpp"
#include "rtpmidid/stats.hpp"
#include "rtpmidid/threading_types.hpp"
#include <chrono>

namespace rtpmidid {
midi_packet_t::midi_packet_t(uint32_t from, const rtpmididns::mididata_t &mididata)
    : from_peer_id(from),
      data(mididata.position, mididata.position + mididata.remaining()),
      timestamp_received(std::chrono::steady_clock::now()) {}
}

namespace rtpmididns {

midipeer_t::~midipeer_t() { stop_thread(); }

bool midipeer_t::control_peer_command(std::string_view cmd, std::string_view params_json,
                                      ::rtpmididns::dmjson::writer_t &out,
                                      std::string &out_error) {
  (void)params_json;
  if (cmd == "help") {
    out.begin_array();
    static const char *const cmds[] = {"status"};
    static const char *const desc[] = {"Return peer status"};
    for (std::size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
      out.array_item();
      out.begin_object();
      out.key("name");
      out.string_value(cmds[i]);
      out.key("description");
      out.string_value(desc[i]);
      out.end_object();
    }
    out.end_array();
    return true;
  }
  if (cmd == "status") {
    dmjson::to_json(status(), out);
    return true;
  }
  ERROR("Unknown command: {}", cmd);
  out_error = "Command not implemented";
  return false;
}

void midipeer_t::start_thread() {
  if (thread_running.load()) {
    DEBUG("[MIDI_FLOW] peer {}: Thread already running", peer_id);
    return;
  }
  thread_running = true;
  DEBUG("[MIDI_FLOW] peer {}: Starting peer thread", peer_id);
  peer_thread = std::thread(&midipeer_t::peer_thread_loop, this);
  DEBUG("[MIDI_FLOW] peer {}: Peer thread started", peer_id);
}

void midipeer_t::stop_thread() {
  if (!thread_running.load()) {
    return;
  }
  thread_running = false;

  rtpmidid::peer_command_t cmd;
  cmd.command = rtpmidid::peer_command_e::SHUTDOWN;
  command_queue.enqueue(cmd);

  thread_wakeup.notify_one();
  if (peer_thread.joinable()) {
    peer_thread.join();
  }
}

void midipeer_t::peer_thread_loop() {
  using namespace std::chrono_literals;

  rtpmidid::block_shutdown_signals();

  try {
    DEBUG("[MIDI_FLOW] peer {}: Peer thread loop started", peer_id);
    while (thread_running.load()) {
      bool processed = false;

      rtpmidid::peer_command_t cmd;
      while (command_queue.dequeue(cmd)) {
        processed = true;
        if (cmd.command == rtpmidid::peer_command_e::SHUTDOWN) {
          return;
        }
      }

      rtpmidid::midi_packet_t packet;
      while (input_queue.dequeue(packet)) {
        processed = true;
        process_midi_packet(packet);
      }

      rtpmidid::routing_request_t request;
      while (output_queue.dequeue(request)) {
        processed = true;
        if (router) {
          if (request.command == rtpmidid::routing_command_e::SEND_MIDI) {
            mididata_t mididata(request.data.data(),
                                static_cast<uint32_t>(request.data.size()));
            router->enqueue_send_midi(peer_id, mididata);
          }
        }
      }

      if (!processed) {
        std::unique_lock<std::mutex> lock(thread_mutex);
        thread_wakeup.wait_for(lock, 10ms, [this] {
          return !thread_running.load() || !input_queue.empty() ||
                 !output_queue.empty() || !command_queue.empty();
        });
      }
    }
    DEBUG("[MIDI_FLOW] peer {}: Peer thread loop exiting normally", peer_id);
  } catch (const std::exception &e) {
    ERROR("[MIDI_FLOW] peer {}: Exception in peer thread loop: {}", peer_id, e.what());
    throw;
  } catch (...) {
    ERROR("[MIDI_FLOW] peer {}: Unknown exception in peer thread loop", peer_id);
    throw;
  }
}

void midipeer_t::process_midi_packet(const rtpmidid::midi_packet_t &packet) {
  packets_recv++;

  const auto t_before_send = std::chrono::steady_clock::now();
  const auto until_send_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      t_before_send - packet.timestamp_received);
  {
    std::lock_guard<std::mutex> lock(internal_latency_mutex_);
    internal_until_send_stats_.add_stat(until_send_ns);
  }
  internal_last_until_send_ns_.store(until_send_ns.count(),
                                     std::memory_order_relaxed);

  mididata_t mididata(const_cast<uint8_t *>(packet.data.data()),
                      static_cast<uint32_t>(packet.data.size()));

  const auto send_start = std::chrono::steady_clock::now();
  send_midi(packet.from_peer_id, mididata);
  const auto send_end = std::chrono::steady_clock::now();
  const auto send_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      send_end - send_start);
  {
    std::lock_guard<std::mutex> lock(internal_latency_mutex_);
    internal_send_midi_stats_.add_stat(send_ns);
  }
  internal_last_send_midi_ns_.store(send_ns.count(), std::memory_order_relaxed);

#ifdef RTPMIDID_ENABLE_TIMING
  DEBUG(
      "[TIMING] peer {}: MIDI until send_midi: {:.3f} ms, send_midi: {:.3f} ms, "
      "size: {} bytes",
      peer_id, until_send_ns.count() / 1e6, send_ns.count() / 1e6,
      packet.data.size());
#endif
}

bool midipeer_t::enqueue_midi_packet(const rtpmidid::midi_packet_t &packet) {
  rtpmidid::midi_packet_t stamped = packet;
  stamped.timestamp_received = std::chrono::steady_clock::now();
  DEBUG("[MIDI_FLOW] peer {}: Enqueueing MIDI packet to input queue, from_peer_id={}, size={} bytes, queue_size={}",
        peer_id, packet.from_peer_id, packet.data.size(), input_queue.size());
  if (!input_queue.enqueue(stamped)) {
    WARNING("[MIDI_FLOW] peer {}: Input queue full, dropping MIDI packet", peer_id);
    return false;
  }
  thread_wakeup.notify_one();
  DEBUG("[MIDI_FLOW] peer {}: MIDI packet enqueued successfully, queue_size={}",
        peer_id, input_queue.size());
  return true;
}

bool midipeer_t::enqueue_command(const rtpmidid::peer_command_t &cmd) {
  if (!command_queue.enqueue(cmd)) {
    WARNING("Command queue full for peer {}", peer_id);
    return false;
  }
  thread_wakeup.notify_one();
  return true;
}

void midipeer_t::enqueue_to_router(const mididata_t &data) {
  if (!router) {
    WARNING("[MIDI_FLOW] peer {}: No router, cannot enqueue MIDI", peer_id);
    return;
  }
  router->enqueue_send_midi(peer_id, data);
}

internal_latency_ms_t midipeer_t::internal_latency_stats() const {
  std::lock_guard<std::mutex> lock(internal_latency_mutex_);
  const auto u = internal_until_send_stats_.average_and_stddev();
  const auto s = internal_send_midi_stats_.average_and_stddev();
  const double last_until_ms =
      internal_last_until_send_ns_.load(std::memory_order_relaxed) / 1e6;
  const double last_send_ms =
      internal_last_send_midi_ns_.load(std::memory_order_relaxed) / 1e6;
  internal_latency_ms_t out = {
    .until_send_midi_ms = {
      .last = last_until_ms,
      .average = u.average.count() / 1e6,
      .stddev = u.stddev.count() / 1e6,
    },
    .send_midi_ms = {
      .last = last_send_ms,
      .average = s.average.count() / 1e6,
      .stddev = s.stddev.count() / 1e6,
    },
  };
  return out;
}

} // namespace rtpmididns

const char *format_as(rtpmididns::midipeer_event_e event) {
  switch (event) {
  case rtpmididns::midipeer_event_e::CONNECTED_ROUTER:
    return "CONNECTED_ROUTER";
  case rtpmididns::midipeer_event_e::DISCONNECTED_ROUTER:
    return "DISCONNECTED_ROUTER";
  case rtpmididns::midipeer_event_e::CONNECTED_PEER:
    return "CONNECTED_PEER";
  case rtpmididns::midipeer_event_e::DISCONNECTED_PEER:
    return "DISCONNECTED_PEER";
  default:
    return "UNKNOWN";
  }
}
