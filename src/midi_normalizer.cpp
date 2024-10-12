/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "midi_normalizer.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/packet.hpp"

// do nothing
#define DEBUG0(...)

namespace rtpmididns {
midi_normalizer_t::midi_normalizer_t() {
  m_buffer.reserve(4); // optimistic about no SysEX in this case, son only quite
                       // small packets
}

midi_normalizer_t::~midi_normalizer_t() {}

void midi_normalizer_t::normalize_stream(
    const rtpmidid::packet_t &packet,
    std::function<void(const rtpmidid::packet_t &)> callback) {
  DEBUG0("Input packet: {}", packet.to_string());
  size_t offset = 0;
  while (offset < packet.get_size()) {
    uint8_t byte = packet.get_uint8(offset);
    parse_midi_byte(byte, callback);
    offset++;
  }
}

void midi_normalizer_t::parse_midi_byte(
    const uint8_t byte,
    std::function<void(const rtpmidid::packet_t &)> callback) {
  DEBUG0("Parsing byte: 0x{:02X}", byte);
  m_buffer.push_back(byte);
  if (waiting_for_size == -1) {
    waiting_for_size = get_size_for_midi_command(byte);
    DEBUG0("Got size: {}", waiting_for_size);
  } else if (waiting_for_size == 0) {
    DEBUG0("Waiting for SysEx end");
    if (byte == 0xF7) {
      DEBUG0("SysEx end");
      rtpmidid::packet_t packet(m_buffer.data(), m_buffer.size());
      callback(packet);
      m_buffer.clear();
      waiting_for_size = -1;
    }
  } else {
    DEBUG0("Waiting for size: {}, at {}", waiting_for_size, m_buffer.size());
    if (m_buffer.size() == (size_t)waiting_for_size) {
      rtpmidid::packet_t packet(m_buffer.data(), m_buffer.size());
      callback(packet);
      m_buffer.clear();
      waiting_for_size = -1;
    }
  }
}

ssize_t midi_normalizer_t::get_size_for_midi_command(uint8_t byte) {
  if ((byte & 0xF0) == 0xF0) {
    if (byte == 0xF0) {
      return 0; // SysEx
    } else {
      return -1; // Invalid
    }
  } else {
    switch (byte & 0xF0) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      return 3;
    case 0xC0:
    case 0xD0:
      return 2;
    default:
      return -1;
    }
  }
}
} // namespace rtpmididns