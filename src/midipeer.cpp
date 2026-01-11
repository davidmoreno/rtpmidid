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
#include "rtpmidid/threading_types.hpp"
#include "json.hpp"
#include <chrono>

// Implement midi_packet_t constructor that needs mididata_t definition
namespace rtpmidid {
midi_packet_t::midi_packet_t(uint32_t from, const rtpmididns::mididata_t &mididata)
    : from_peer_id(from),
      data(mididata.position, mididata.position + mididata.remaining()) {}
}

namespace rtpmididns {

midipeer_t::~midipeer_t() {
  stop_thread();
  // Don't call router->remove_peer() here - it can cause deadlocks
  // The router should handle peer removal through enqueue_remove_peer()
  // or the peer should be removed before destruction
}
json_t midipeer_t::command(const std::string &cmd, const json_t &data) {
  ERROR("Unknown command: {}", cmd);
  if (cmd == "help") {
    return {json_t::object({})};
  }
  if (cmd == "status") {
    return status();
  }
  return json_t({
      {"error", "Command not implemented"},
  });
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
  
  // Send shutdown command
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
  
  try {
    DEBUG("[MIDI_FLOW] peer {}: Peer thread loop started", peer_id);
    while (thread_running.load()) {
    bool processed = false;
    
    // Process commands first (highest priority)
    rtpmidid::peer_command_t cmd;
    while (command_queue.dequeue(cmd)) {
      processed = true;
      if (cmd.command == rtpmidid::peer_command_e::SHUTDOWN) {
        return;
      }
      // Other commands can be handled by subclasses
    }
    
    // Process incoming MIDI packets
    size_t queue_size_before = input_queue.size();
    if (queue_size_before > 0) {
      DEBUG("[MIDI_FLOW] peer {}: Checking input queue, size={} packets", peer_id, queue_size_before);
    }
    rtpmidid::midi_packet_t packet;
    int dequeued_count = 0;
    while (input_queue.dequeue(packet)) {
      processed = true;
      dequeued_count++;
      DEBUG("[MIDI_FLOW] peer {}: Dequeued MIDI packet #{} from input queue, from_peer_id={}, size={} bytes, remaining_in_queue={}",
            peer_id, dequeued_count, packet.from_peer_id, packet.data.size(), input_queue.size());
      process_midi_packet(packet);
    }
    if (queue_size_before > 0 && dequeued_count == 0) {
      WARNING("[MIDI_FLOW] peer {}: Input queue has {} packets but dequeue() returned false! Queue might be corrupted",
             peer_id, queue_size_before);
    }
    
    // Process outgoing routing requests
    rtpmidid::routing_request_t request;
    while (output_queue.dequeue(request)) {
      processed = true;
      if (router) {
        // Enqueue to router (this will be handled by router thread)
        if (request.command == rtpmidid::routing_command_e::SEND_MIDI) {
          mididata_t mididata(request.data.data(), 
                             static_cast<uint32_t>(request.data.size()));
          router->enqueue_send_midi(peer_id, mididata);
        }
        // Other routing commands can be handled similarly
      }
    }
    
    // Sleep if no work
    if (!processed) {
      DEBUG("[MIDI_FLOW] peer {}: No work, sleeping. input_queue_size={}, output_queue_size={}, command_queue_size={}",
            peer_id, input_queue.size(), output_queue.size(), command_queue.size());
      std::unique_lock<std::mutex> lock(thread_mutex);
      thread_wakeup.wait_for(lock, 10ms, [this] {
        bool should_wake = !thread_running.load() || 
               !input_queue.empty() || 
               !output_queue.empty() || 
               !command_queue.empty();
        if (should_wake && !input_queue.empty()) {
          DEBUG("[MIDI_FLOW] peer {}: Waking up due to input queue not empty, size={}", 
                peer_id, input_queue.size());
        }
        return should_wake;
      });
    } else {
      DEBUG("[MIDI_FLOW] peer {}: Processed work, continuing loop", peer_id);
    }
    }
    DEBUG("[MIDI_FLOW] peer {}: Peer thread loop exiting normally", peer_id);
  } catch (const std::exception &e) {
    ERROR("[MIDI_FLOW] peer {}: Exception in peer thread loop: {}", peer_id, e.what());
    throw; // Re-throw to terminate thread
  } catch (...) {
    ERROR("[MIDI_FLOW] peer {}: Unknown exception in peer thread loop", peer_id);
    throw;
  }
}

void midipeer_t::process_midi_packet(const rtpmidid::midi_packet_t &packet) {
  packets_recv++;
  mididata_t mididata(const_cast<uint8_t *>(packet.data.data()), 
                     static_cast<uint32_t>(packet.data.size()));
  DEBUG("[MIDI_FLOW] peer {}: Processing MIDI packet, calling send_midi(), size={} bytes",
        peer_id, mididata.size());
  send_midi(packet.from_peer_id, mididata);
}

bool midipeer_t::enqueue_midi_packet(const rtpmidid::midi_packet_t &packet) {
  DEBUG("[MIDI_FLOW] peer {}: Enqueueing MIDI packet to input queue, from_peer_id={}, size={} bytes, queue_size={}",
        peer_id, packet.from_peer_id, packet.data.size(), input_queue.size());
  if (!input_queue.enqueue(packet)) {
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
  DEBUG("[MIDI_FLOW] peer {}: Enqueueing MIDI to router, size={} bytes", 
        peer_id, data.size());
  router->enqueue_send_midi(peer_id, data);
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
