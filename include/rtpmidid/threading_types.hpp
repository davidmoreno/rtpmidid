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

#include "iobytes.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace rtpmididns {
// Forward declaration - actual definition in src/mididata.hpp
class mididata_t;
} // namespace rtpmididns

namespace rtpmidid {

/**
 * @short MIDI packet for queue transport.
 *
 * Owned MIDI bytes plus the source peer id; carried inside
 * `router_command_t::send_midi_t` and the peer queue's `process_midi_t` /
 * `send_to_router_t` alternatives.
 */
struct midi_packet_t {
  uint32_t from_peer_id;
  std::vector<uint8_t> data;
  std::chrono::steady_clock::time_point timestamp_received;

  midi_packet_t()
      : from_peer_id(0), timestamp_received(std::chrono::steady_clock::now()) {}
  midi_packet_t(uint32_t from, const rtpmididns::mididata_t &mididata);
  midi_packet_t(uint32_t from, const uint8_t *bytes, size_t size)
      : from_peer_id(from), data(bytes, bytes + size),
        timestamp_received(std::chrono::steady_clock::now()) {}

  midi_packet_t(const midi_packet_t &other) = default;
  midi_packet_t(midi_packet_t &&other) noexcept
      : from_peer_id(other.from_peer_id), data(std::move(other.data)),
        timestamp_received(other.timestamp_received) {}

  midi_packet_t &operator=(const midi_packet_t &other) = default;
  midi_packet_t &operator=(midi_packet_t &&other) noexcept {
    if (this != &other) {
      from_peer_id = other.from_peer_id;
      data = std::move(other.data);
      timestamp_received = other.timestamp_received;
    }
    return *this;
  }

  template <typename MididataT> MididataT to_mididata() const {
    return MididataT(const_cast<uint8_t *>(data.data()),
                     static_cast<uint32_t>(data.size()));
  }
};

} // namespace rtpmidid
