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

#include <fmt/ostream.h>
#include <rtpmidid/logger.hpp>
#include <stdio.h>
#include <string>
#include <string_view>

namespace logger {
logger __logger;

enum Color {
  RED = 31,
  GREEN = 32,
  YELLOW = 33,
  BLUE = 34,
  PURPLE = 35,
  ORANGE = 36,
  WHITE = 37,
};

std::string color(const std::string_view &str, Color color,
                  bool highlight = false) {
  int hl = highlight ? 1 : 0;
  return fmt::format("\033[{};{}m{}\033[0m", hl, color, str);
}
std::string color(const std::string_view &str, Color color, Color bgcolor,
                  bool highlight = false) {
  int hl = highlight ? 1 : 0;
  return fmt::format("\033[{};{};{}m{}\033[0m", hl, color, bgcolor, str);
}

logger::logger() {}
logger::~logger() {}
void logger::log(const char *filename, int lineno, LogLevel loglevel,
                 const std::string &msg) {
  time_t now;
  time(&now);
  char timestamp[sizeof "2011-10-08T07:07:09Z"];
  strftime(timestamp, sizeof timestamp, "%FT%TZ", gmtime(&now));

  auto my_color = WHITE;
  switch (loglevel) {
  case DEBUG:
    my_color = BLUE;
    break;
  case WARNING:
    my_color = YELLOW;
    break;
  case ERROR:
    my_color = RED;
    break;
  case INFO:
    my_color = WHITE;
    break;
  case SUCCESS:
    my_color = GREEN;
    break;
  }

  fmt::print(
      "{} {}\n",
      color(fmt::format("[{}] [{}:{}]", timestamp, filename, lineno), my_color),
      msg); // color("msg"), RED);
}
void logger::flush() { ::fflush(::stderr); }
} // namespace logger
