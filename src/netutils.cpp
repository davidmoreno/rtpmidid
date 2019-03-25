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

uint32_t parse_uint32(parse_buffer_t &buffer){
  buffer.check_enought(4);
  auto data = buffer.position;
  return ((uint32_t)data[0]<<24) + ((uint32_t)data[1]<<16) + ((uint32_t)data[2]<< 8) + ((uint32_t)data[3]);
  buffer.position += 4;
}

uint16_t parse_uint16(parse_buffer_t &buffer){
  buffer.check_enought(2);
  auto data = buffer.position;
  return ((uint16_t)data[0]<< 8) + ((uint16_t)data[1]);
  buffer.position += 2;
}

void print_hex(parse_buffer_t &buffer, bool to_pos){
  auto data = buffer.start;
  auto n = (to_pos ? buffer.position : buffer.end) - data;
  for( int i=0 ; i<n ; i++ ){
    printf("%02X ", data[i] & 0x0FF);
    if (i % 4 == 3)
      printf(" ");
    if (i % 16 == 15)
      printf("\n");
  }
  printf("\n");
  for( int i=0 ; i<n ; i++ ){
    if (isprint(data[i])){
      printf("%c", data[i]);
    }
    else{
      printf(".");
    }
    if (i % 4 == 3)
      printf(" ");
    if (i % 16 == 15)
      printf("\n");
  }
  printf("\n");
}

void write_uint16(parse_buffer_t &data, uint16_t n){
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
}

void write_uint32(parse_buffer_t &data, uint32_t n){
  *data++ = (n>>24) & 0x0FF;
  *data++ = (n>>16) & 0x0FF;
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
}

void write_str0(parse_buffer_t &data, const std::string_view &view){
  for (auto c: view){
    *data++ = c;
  }
  *data++ = '\0';
}


void raw_write_uint16(uint8_t *data, uint16_t n){
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
}

}
