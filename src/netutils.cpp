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

uint32_t parse_uint32(const uint8_t *buffer){
  return ((uint32_t)buffer[0]<<24) + ((uint32_t)buffer[1]<<16) + ((uint32_t)buffer[2]<< 8) + ((uint32_t)buffer[3]);
}

uint16_t parse_uint16(const uint8_t *buffer){
  return ((uint16_t)buffer[0]<< 8) + ((uint16_t)buffer[1]);
}

void print_hex(const uint8_t *data, int n){
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

uint8_t *write_uint16(uint8_t *data, uint16_t n){
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
  return data;
}

uint8_t *write_uint32(uint8_t *data, uint32_t n){
  *data++ = (n>>24) & 0x0FF;
  *data++ = (n>>16) & 0x0FF;
  *data++ = (n>>8) & 0x0FF;
  *data++ = (n & 0x0FF);
  return data;
}

uint8_t *write_str0(uint8_t *data, const std::string_view &view){
  for (auto c: view){
    *data++ = c;
  }
  *data++ = '\0';
  return data;
}

}
