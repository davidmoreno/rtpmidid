/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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
#include <unistd.h>

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

const char *color(const char *str, Color color, bool highlight = false) {
  int hl = highlight ? 1 : 0;
  static char buffer[256];
  auto n = fmt::format_to_n(buffer, sizeof(buffer), "\033[{};{}m{}\033[0m", hl,
                            (int)color, str);
  *n.out = '\0';
  return buffer;
}
const char *color(const char *str, Color color, Color bgcolor,
                  bool highlight = false) {
  int hl = highlight ? 1 : 0;
  static char buffer[256];
  auto n = fmt::format_to_n(buffer, sizeof(buffer), "\033[{};{};{}m{}\033[0m",
                            hl, (int)color, (int)bgcolor, str);
  *n.out = '\0';
  return buffer;
}

logger::logger() { is_a_terminal = isatty(fileno(stdout)); }

logger::~logger() {}
void logger::log(const char *filename, int lineno, LogLevel loglevel,
                 const char *msg) {
  static char buffer[512];
  if (is_a_terminal) {
    time_t now = time(nullptr);
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

    auto n = fmt::format_to_n(buffer, sizeof(buffer), "[{}] [{}:{}]", timestamp,
                              filename, lineno);
    *n.out = '\0';
    n = fmt::format_to_n(buffer, sizeof(buffer), "{} {}\n",
                         color(buffer, my_color),
                         msg); // color("msg"), RED);
    *n.out = '\0';
  } else {
    auto n =
        fmt::format_to_n(buffer, sizeof(buffer), " sizeof(buffer),[{}:{}] {}\n",
                         filename, lineno, msg);
    *n.out = '\0';
  }
  ::fprintf(stderr, "%s", buffer);
}
void logger::flush() { ::fflush(stderr); }
} // namespace logger
