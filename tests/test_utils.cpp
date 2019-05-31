
/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#include "./test_utils.hpp"

static int char_to_nibble(char c){
  if (c >= '0' && c <= '9'){
    return c - '0';
  }
  if (c >= 'A' && c <= 'F'){
    return 10 + c - 'A';
  }
  ERROR("{} is not an HEX number", c);
  throw std::exception();
}

managed_parse_buffer_t hex_to_bin(const std::string &str){
  managed_parse_buffer_t buffer(str.length()); // max size. Normally around 1/2

  // A state machine that alternates between most significant nibble, and least significant nibble
  bool msn = false;
  bool quote = false;
  int lastd = 0;
  for (char c: str){
    if (quote){
      if (c == '\''){
        quote = false;
      } else {
        buffer.buffer.write_uint8(c);
      }
    }
    else if (c == '\''){
      quote = true;
    }
    else if (!isalnum(c)) {
      // skip non alnum
      continue;
    }
    else if (!msn){
      lastd = char_to_nibble(c) << 4;
      msn = true;
    } else {
      lastd |= char_to_nibble(c);
      buffer.buffer.write_uint8(lastd);
      msn = false;
    }
  }

  // Revert to read mode
  buffer.buffer.end = buffer.buffer.position;
  buffer.buffer.position = buffer.buffer.start;

  // buffer.buffer.print_hex(true);

  return buffer;
}
