/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
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
#include <exception>
#include <fmt/format.h>
#include <string>

namespace rtpmidid {
class exception : public std::exception {
  std::string msg;

public:
  template <typename... Args> exception(Args... args) {
    msg = fmt::format(args...);
  }
  const char *what() const noexcept { return msg.c_str(); }
};

class not_implemented : public std::exception {
public:
  const char *what() const noexcept { return "Not Implemented"; }
};
} // namespace rtpmidid
