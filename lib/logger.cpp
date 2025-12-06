/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include <rtpmidid/logger.hpp>
#include <algorithm>
#include <rtpmidid/exceptions.hpp>
#include <string>

namespace rtpmidid {
rtpmidid::logger_t logger2;
static constexpr const char *ansi_color(logger_level_t level) {
  switch (level) {
  case DEBUG:
    return "\033[1;34m";
  case INFO:
    return ""; // no color
  case WARNING:
    return "\033[1;33m";
  case ERROR:
    return "\033[1;31m";
  default:
    return "";
  }
}
static constexpr size_t ansi_color_length(logger_level_t level) {
  switch (level) {
  case DEBUG:
    return 7;
  case INFO:
    return 0;
  case WARNING:
    return 7;
  case ERROR:
    return 7;
  default:
    return 0;
  }
}

static constexpr const char *ansi_color_reset() { return "\033[0m"; }

static constexpr const char *basename(const char *filename) {
  const char *p = filename;
  while (*filename) {
    if (*filename == '/') {
      p = filename + 1;
    }
    filename++;
  }
  return p;
}

logger_t::buffer_t::iterator
logger_t::log_preamble(logger_level_t level, const char *filename, int lineno) {
  auto it = buffer.begin();

  it = FMT::format_to(it, "{}[{}] {}:{}", ansi_color(level), level,
                      basename(filename), lineno);
  for (int i = it - buffer.begin() - ansi_color_length(level); i < 40; i++) {
    *it = ' ';
    it++;
  }
  it = FMT::format_to(it, " | ");
  return it;
}

void logger_t::log_postamble(buffer_t::iterator it) {
  it = FMT::format_to(it, "{}", ansi_color_reset());
  *it = '\0';
  std::cout << buffer.data() << std::endl;
}

logger_level_t str_to_log_level(const std::string &value) {
  // Check numeric values first (they don't need case conversion)
  if (value == "0") {
    return logger_level_t::DEBUG;
  }
  if (value == "1") {
    return logger_level_t::INFO;
  }
  if (value == "2") {
    return logger_level_t::WARNING;
  }
  if (value == "3") {
    return logger_level_t::ERROR;
  }

  // Convert value to lowercase for case-insensitive comparison
  std::string value_lowercase;
  value_lowercase.resize(value.size());
  std::transform(value.begin(), value.end(), value_lowercase.begin(),
                 ::tolower);

  if (value_lowercase == "debug") {
    return logger_level_t::DEBUG;
  }
  if (value_lowercase == "info") {
    return logger_level_t::INFO;
  }
  if (value_lowercase == "warning") {
    return logger_level_t::WARNING;
  }
  if (value_lowercase == "error") {
    return logger_level_t::ERROR;
  }
  throw rtpmidid::exception("Invalid log level value: {}. Valid values: debug, info, warning, error, or 0-3", value);
}

}; // namespace rtpmidid
