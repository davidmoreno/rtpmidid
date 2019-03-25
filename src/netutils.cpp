/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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

#include "./netutils.hpp"
#include <stdio.h>
#include <ctype.h>

namespace rtpmidid{

uint8_t *raw_write_uint16(uint8_t *data, uint16_t n){
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
  return data;
}

// Reads a label from the origin parse buffer and stores on the label parse buffer
void read_label(parse_buffer_t &buffer, parse_buffer_t &label){
  // DEBUG(
  //   "Read label start: {:p}, end: {:p}, base: {:p}, str: {:p}, str_end: {:p}",
  //   start, end, base, str, str_end
  // );
  uint8_t *data = buffer.position;
  uint8_t *end = buffer.end;
  uint8_t *base = buffer.start;
  uint8_t *start = buffer.position;

  uint8_t *str = label.position;
  uint8_t *str_end = label.end;

  bool first = true;
  while(data < end && str < str_end){
    uint8_t nchars = *data;
    if (nchars == 192){
      data++;
      if (base + *data > start){
        throw exception("Invalid package. Label pointer out of bounds. Max pos is begining current record.");
      }
      if (first)
        first = false;
      else
        *str++ = '.';
      *str = 0;
      // DEBUG("Label is compressed, refers to {}. So far read: {} bytes, <{}>", *data, nbytes, str - nbytes);
      buffer.position = data;
      label.position = str;
      read_label(buffer, label);
      return;
    }
    data++;
    if (nchars == 0){
      *str = 0;
      return;
    }
    if (first)
      first = false;
    else
      *str++ = '.';
    for (int i=0; i< nchars; i++){
      *str++ = *data++;
    }
  }
  buffer.print_hex();
  throw exception("Invalid package. Label out of bounds at {}.", data - base);
}

// Not prepared for pointers yet. Lazy, but should work ok,
void write_label(parse_buffer_t &data, const std::string_view &name){
  auto strI = name.begin();
  auto endI = name.end();
  for(auto I=strI; I < endI; ++I){
    if (*I == '.'){
      *data++ = I - strI;
      for( ; strI<I ; ++strI ){
        *data++ = *strI;
      }
      strI++;
    }
  }
  *data++ = endI - strI;
  for( ; strI<endI ; ++strI ){
    *data++ = *strI;
  }
  // end of labels
  *data++ = 0;
}

}
