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

#include <fmt/core.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace rtpmidid {

enum packet_type_e {
  UNKNOWN = 255,
  MIDI = 1,
  COMMAND = 2,
};

class packet_t {
protected:
  uint8_t *data;
  size_t size;

public:
  packet_t(const packet_t &packet) : data(packet.data), size(packet.size) {}
  packet_t(uint8_t *data, size_t size) : data(data), size(size) {}

  uint8_t *get_data() { return data; }
  uint8_t *get_data() const { return data; }
  size_t get_size() const { return size; }

  static packet_type_e get_packet_type(const uint8_t *data, size_t size);
  packet_type_e get_packet_type() const;
};
} // namespace rtpmidid

// allow fmt to format rtpmidid::packet_type_e
template <> struct fmt::formatter<rtpmidid::packet_type_e> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::packet_type_e &c, FormatContext &ctx) {
    switch (c) {
    case rtpmidid::packet_type_e::UNKNOWN:
      return format_to(ctx.out(), "UNKNOWN");
    case rtpmidid::packet_type_e::MIDI:
      return format_to(ctx.out(), "MIDI");
    case rtpmidid::packet_type_e::COMMAND:
      return format_to(ctx.out(), "COMMAND");
    default:
      return format_to(ctx.out(), "Unknown Packet Type:{}", c);
    }
  }
};

// allow fmt to format rtpmidid::packet_t
template <> struct fmt::formatter<rtpmidid::packet_t> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::packet_t &c, FormatContext &ctx) {
    return format_to(ctx.out(), "Packet: {} bytes", c.get_size());
  }
};