/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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


#include <ctype.h>
#include <stdio.h>

#include "netutils.hpp"

namespace mdns {

uint8_t *raw_write_uint16(uint8_t *data, uint16_t n) {
  *data++ = (n >> 8) & 0x0FF;
  *data++ = (n & 0x0FF);
  return data;
}

// Reads a label from the origin parse buffer and stores on the label parse
// buffer
void read_label(parse_buffer_t &buffer, parse_buffer_t &label) {
  // DEBUG(
  //   "Read label start: {:p}, end: {:p}, base: {:p}, str: {:p}, str_end:
  //   {:p}", start, end, base, str, str_end
  // );
  bool first = true;
  while (true) {
    uint8_t nchars = buffer.read_uint8();
    if (nchars == 192) {
      auto position_pointer = buffer.read_uint8();
      auto buffer_rec = buffer;
      buffer_rec.position = buffer.start + position_pointer;
      buffer_rec.assert_valid_position();

      if (!first)
        label.write_uint8('.');
      // DEBUG("Label is compressed, refers to {}. So far read: {} bytes, <{}>",
      // *data, nbytes, str - nbytes);
      read_label(buffer_rec, label);
      return;
    }
    if (nchars == 0) {
      label.write_uint8('\0');
      return;
    }
    if (first)
      first = false;
    else
      label.write_uint8('.');
    label.copy_from(buffer, nchars);
  }
}

// Not prepared for pointers yet. Lazy, but should work ok,
void write_label(parse_buffer_t &buffer, const std::string_view &name) {
  auto strI = name.begin();
  auto endI = name.end();
  buffer.check_enough(name.length() +
                      1); // I will change the . for lengths, so same length + 1
  uint8_t *data = buffer.position;
  for (auto I = strI; I < endI; ++I) {
    if (*I == '.') {
      *data++ = I - strI;
      for (; strI < I; ++strI) {
        *data++ = *strI;
      }
      strI++;
    }
  }
  *data++ = endI - strI;
  for (; strI < endI; ++strI) {
    *data++ = *strI;
  }
  // end of labels
  *data++ = 0;
  buffer.position = data;
}

} // namespace rtpmidid
