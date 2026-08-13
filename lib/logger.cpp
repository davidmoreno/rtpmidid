/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
void (*logger_log_sink)(log_message_t) = nullptr;

/// The per-thread log tag: set once per thread (e.g. by the actor runtime
/// at thread start); `log()` copies it into every emitted message so the
/// tag travels with the message to the rendering thread.
static thread_local std::string thread_log_tag;

void set_log_thread_tag(std::string name) { thread_log_tag = std::move(name); }

const std::string &current_log_thread_tag() { return thread_log_tag; }

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
static constexpr const char *ansi_color_reset() { return "\033[0m"; }

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

std::string logger_format_line(const log_message_t &msg) {
  // "[{level}] [tag] {basename}:{lineno}" with the non-color prefix
  // padded to 40 columns, then " | ", the body and the color reset. An
  // empty tag renders byte-identical to the pre-tag output.
  std::string prefix = "[" + FMT::format("{}", msg.level) + "] ";
  if (!msg.thread_name.empty()) {
    prefix += "[" + msg.thread_name + "] ";
  }
  prefix += msg.origin;
  std::string out;
  out.reserve(ansi_color_length(msg.level) + 40 + 3 + msg.text.size() + 8 +
              msg.thread_name.size() + 3);
  out += ansi_color(msg.level);
  out += prefix;
  if (prefix.size() < 40) {
    out.append(40 - prefix.size(), ' ');
  }
  out += " | ";
  out += msg.text;
  out += ansi_color_reset();
  return out;
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
