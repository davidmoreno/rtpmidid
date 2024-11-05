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

#include <array>
#include <rtpmidid/logger.hpp>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace rtpmidid {

enum packet_type_e {
  UNKNOWN = 255,
  MIDI = 1,
  COMMAND,
};

class packet_t {
protected:
  uint8_t *data;
  size_t size;

public:
  packet_t(const packet_t &packet) : data(packet.data), size(packet.size) {}
  packet_t(uint8_t *data, size_t size) : data(data), size(size) {}
  template <typename T>
  packet_t(T &data) : data(data.data()), size(data.size()) {}

  uint8_t *get_data() { return data; }
  uint8_t *get_data() const { return data; }
  size_t get_size() const { return size; }

  uint8_t get_uint8(size_t offset) const { return data[offset]; }
  uint16_t get_uint16(size_t offset) const {
    return (data[offset] << 8) | data[offset + 1];
  }
  uint32_t get_uint32(size_t offset) const {
    return (data[offset] << 24) | (data[offset + 1] << 16) |
           (data[offset + 2] << 8) | data[offset + 3];
  }
  uint64_t get_uint64(size_t offset) const {
    return ((uint64_t(data[offset]) << 56) |
            (uint64_t(data[offset + 1]) << 48) |
            (uint64_t(data[offset + 2]) << 40) |
            (uint64_t(data[offset + 3]) << 32) |
            (uint64_t(data[offset + 4]) << 24) |
            (uint64_t(data[offset + 5]) << 16) |
            (uint64_t(data[offset + 6]) << 8) | uint64_t(data[offset + 7]));
  }

  void set_uint8(size_t offset, uint8_t value) { data[offset] = value; }
  void set_uint16(size_t offset, uint16_t value) {
    data[offset] = value >> 8;
    data[offset + 1] = value & 0xFF;
  }
  void set_uint32(size_t offset, uint32_t value) {
    data[offset] = value >> 24;
    data[offset + 1] = (value >> 16) & 0xFF;
    data[offset + 2] = (value >> 8) & 0xFF;
    data[offset + 3] = value & 0xFF;
  }
  void set_uint64(size_t offset, uint64_t value) {
    data[offset] = (value >> 56) & 0xFF;
    data[offset + 1] = (value >> 48) & 0xFF;
    data[offset + 2] = (value >> 40) & 0xFF;
    data[offset + 3] = (value >> 32) & 0xFF;
    data[offset + 4] = (value >> 24) & 0xFF;
    data[offset + 5] = (value >> 16) & 0xFF;
    data[offset + 6] = (value >> 8) & 0xFF;
    data[offset + 7] = (value) & 0xFF;
  }

  static packet_type_e get_packet_type(const uint8_t *data, size_t size);
  packet_type_e get_packet_type() const;

  packet_t slice(size_t offset, size_t length) const {
    if (offset + length > size) {
      throw std::runtime_error("Slice out of bounds");
    }
    return packet_t(data + offset, length);
  }

  std::string to_string() const {
    std::string ret = "";
    int block_chars = 4;
    int line_chars = 16;

    for (size_t i = 0; i < size; i++) {
      if (block_chars == 0) {
        ret += " ";
        block_chars = 4;
      }
      if (line_chars == 0) {
        ret += "\n";
        line_chars = 16;
      }
      block_chars--;
      line_chars--;
      ret += FMT::format("{:02X} ", data[i]);
    }
    return ret;
  }
};

template <size_t SIZE> class packet_managed_t : public packet_t {
  std::array<uint8_t, SIZE> buffer;

public:
  packet_managed_t() : packet_t(nullptr, 0) {
    data = buffer.data();
    size = SIZE;
  }

  void copy_from(const packet_t &packet) {
    if (packet.get_size() > SIZE) {
      throw std::runtime_error("Packet too big");
    }
    memcpy(buffer.data(), packet.get_data(), packet.get_size());
    size = packet.get_size();
  };
  void clear() { size = 0; }
};

} // namespace rtpmidid

BASIC_FORMATTER(rtpmidid::packet_t, "Packet: {} bytes", v.get_size());

ENUM_FORMATTER_BEGIN(rtpmidid::packet_type_e);
ENUM_FORMATTER_ELEMENT(rtpmidid::packet_type_e::UNKNOWN, "UNKNOWN");
ENUM_FORMATTER_ELEMENT(rtpmidid::packet_type_e::MIDI, "MIDI");
ENUM_FORMATTER_ELEMENT(rtpmidid::packet_type_e::COMMAND, "COMMAND");
ENUM_FORMATTER_END();
