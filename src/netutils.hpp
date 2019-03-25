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

#pragma once
#include <cstdint>
#include <string_view>
#include "./exceptions.hpp"

namespace rtpmidid{
  struct parse_buffer_t {
    uint8_t *start;
    uint8_t *end;
    uint8_t *position;

    parse_buffer_t(uint8_t *start, uint8_t *end, uint8_t *position){
      this->start = start;
      this->end = end;
      this->position = position;
    }
    parse_buffer_t(uint8_t *start, uint32_t size){
      this->start = start;
      this->end = start + size;
      this->position = start;
    }

    void check_enought(int nbytes){
      if (position + nbytes >= end)
        throw exception("Try to access end of buffer");
    }
    void assert_valid_position(){
      if (position >= end)
        throw exception("Invalid buffer position");
    }

    uint8_t &operator*(){
      return *position;
    }
    parse_buffer_t &operator++(int _postincr_dummy){
      position++;
      if (position >= end)
        throw exception("Try to access end of buffer");

      return *this;
    }
    uint32_t length(){
      return position - start;
    }
  };

  uint32_t parse_uint32(parse_buffer_t &buffer);
  uint16_t parse_uint16(parse_buffer_t &buffer);

  void raw_write_uint16(uint8_t *data, uint16_t n);
  void write_uint16(parse_buffer_t &data, uint16_t n);
  void write_uint32(parse_buffer_t &data, uint32_t n);
  void write_str0(parse_buffer_t &data, const std::string_view &str);

  void print_hex(parse_buffer_t &, bool to_pos=true);
}
