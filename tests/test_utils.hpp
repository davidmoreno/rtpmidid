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
#pragma once

#include "../src/netutils.hpp"

class managed_parse_buffer_t {
public:
  std::vector<uint8_t> data;
  rtpmidid::parse_buffer_t buffer;

  managed_parse_buffer_t(int size) : data(size), buffer(nullptr, 0){
    buffer = rtpmidid::parse_buffer_t(data.data(), size);
  }

  rtpmidid::parse_buffer_t &operator*(){
    return buffer;
  }
};

managed_parse_buffer_t hex_to_bin(const std::string &str);
