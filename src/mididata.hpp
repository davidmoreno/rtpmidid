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
#include "rtpmidid/iobytes.hpp"
#include <fmt/core.h>
#include <string_view>

namespace rtpmididns {

class mididata_t : public rtpmidid::io_bytes_reader {
public:
  mididata_t(uint8_t *data, uint32_t size)
      : rtpmidid::io_bytes_reader(data, size) {}

  mididata_t(rtpmidid::io_bytes_writer &writer)
      : rtpmidid::io_bytes_reader(writer.start,
                                  writer.position - writer.start) {}
  mididata_t(const rtpmidid::io_bytes_reader &reader)
      : rtpmidid::io_bytes_reader(reader.position, reader.end - reader.start) {}
};
} // namespace rtpmididns

template <>
struct fmt::formatter<rtpmididns::mididata_t> : formatter<fmt::string_view> {
  auto format(const rtpmididns::mididata_t &data, format_context &ctx) {

    return fmt::format_to(ctx.out(), "[mididata_t {} + {}, at {}]",
                          (void *)data.start, data.size(), data.pos());
  }
};
