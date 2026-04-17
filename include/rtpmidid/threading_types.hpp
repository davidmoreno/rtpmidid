/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "iobytes.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>

namespace rtpmididns {
// Forward declaration - actual definition in src/mididata.hpp
class mididata_t;
}

namespace rtpmidid {

/**
 * @short MIDI packet for queue transport
 *
 * Stores MIDI data with source peer ID for routing.
 * Uses vector to own the data since mididata_t is just a view.
 * Includes timing information for latency measurement.
 */
struct midi_packet_t {
  uint32_t from_peer_id;
  // MIDI data stored as raw bytes (owned by this struct)
  // This is necessary because mididata_t is just a view
  std::vector<uint8_t> data;
  // High-resolution timestamp when packet was received/created
  std::chrono::steady_clock::time_point timestamp_received;

  midi_packet_t() : from_peer_id(0)
#ifdef RTPMIDID_ENABLE_TIMING
        ,
        timestamp_received(std::chrono::steady_clock::now())
#endif
  {
  }
  // Constructor from mididata_t - implementation in .cpp file to avoid include
  midi_packet_t(uint32_t from, const rtpmididns::mididata_t &mididata);
  midi_packet_t(uint32_t from, const uint8_t *bytes, size_t size)
      : from_peer_id(from), data(bytes, bytes + size)
#ifdef RTPMIDID_ENABLE_TIMING
        ,
        timestamp_received(std::chrono::steady_clock::now())
#endif
  {
  }
  
  // Copy constructor - ensure vector is properly copied
  midi_packet_t(const midi_packet_t &other) = default;
  
  // Move constructor - ensure vector is properly moved
  midi_packet_t(midi_packet_t &&other) noexcept 
      : from_peer_id(other.from_peer_id), 
        data(std::move(other.data)),
        timestamp_received(other.timestamp_received) {}
  
  // Copy assignment
  midi_packet_t &operator=(const midi_packet_t &other) = default;
  
  // Move assignment
  midi_packet_t &operator=(midi_packet_t &&other) noexcept {
    if (this != &other) {
      from_peer_id = other.from_peer_id;
      data = std::move(other.data);
      timestamp_received = other.timestamp_received;
    }
    return *this;
  }
  
  // Convert to mididata_t view (caller must include mididata.hpp)
  // This is a helper that requires the actual mididata_t definition
  template<typename MididataT>
  MididataT to_mididata() const {
    return MididataT(
        const_cast<uint8_t *>(data.data()),
        static_cast<uint32_t>(data.size()));
  }
};

/**
 * @short Routing request command
 */
enum class routing_command_e : uint8_t {
  SEND_MIDI = 1,      // Route MIDI packet
  CONNECT = 2,        // Connect two peers
  DISCONNECT = 3,     // Disconnect two peers
  ADD_PEER = 4,       // Add new peer
  REMOVE_PEER = 5,    // Remove peer
  PEER_EVENT = 6,     // Send event to peer
};

/**
 * @short Routing request structure
 */
struct routing_request_t {
  routing_command_e command;
  uint32_t from_peer_id;
  uint32_t to_peer_id; // 0 for broadcast or N/A
  std::vector<uint8_t> data; // MIDI data or other payload

  routing_request_t() : command(routing_command_e::SEND_MIDI), from_peer_id(0), to_peer_id(0) {}
};

/**
 * @short Peer command types
 */
enum class peer_command_e : uint8_t {
  SHUTDOWN = 1,
  STATUS = 2,
  ROUTER_COMMAND = 3, // Command from control socket
};

/**
 * @short Command for peer thread
 */
struct peer_command_t {
  peer_command_e command;
  std::vector<uint8_t> data; // Command-specific data (JSON, etc.)

  peer_command_t() : command(peer_command_e::SHUTDOWN) {}
};

} // namespace rtpmidid
