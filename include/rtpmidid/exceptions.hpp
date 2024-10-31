/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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
#include <cstring>
#include <exception>
#include <format>
#include <string>

namespace rtpmidid {
class exception : public std::exception {
  std::string msg;

public:
  template <typename... Args>
  exception(std::format_string<Args...> msg, Args... args)
      : msg(std::format(msg, std::forward<Args>(args)...)) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

class not_implemented : public std::exception {
public:
  const char *what() const noexcept override { return "Not Implemented"; }
};
class network_exception : public std::exception {
  std::string str;
  int errno_ = 0;

public:
  network_exception(int _errno) : errno_(_errno) {
    str = std::format("Network error {} ({})", strerror(errno_), errno_);
  }
  const char *what() const noexcept override { return str.c_str(); }
};

class ini_exception : public exception {
public:
  template <typename... Args>
  ini_exception(const std::string &filename, int lineno,
                std::format_string<Args...> msg, Args... args)
      : exception("Error parsing INI configuration at {}:{}: {}", filename,
                  lineno, std::format(msg, std::forward<Args>(args)...)) {}
};
} // namespace rtpmidid
