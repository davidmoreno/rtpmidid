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

namespace rtpmidid{
  uint32_t parse_uint32(const uint8_t *buffer);
  uint16_t parse_uint16(const uint8_t *buffer);

  uint8_t *write_uint16(uint8_t *data, uint16_t n);
  uint8_t *write_uint32(uint8_t *data, uint32_t n);
  uint8_t *write_str0(uint8_t *data, const std::string_view &str);

  void print_hex(const uint8_t *data, int n);
}
