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
#include <atomic>
#include <rtpmidid/exceptions.hpp>
#include <string>
#include <string_view>

namespace rtpmidid {
rtpmidid::logger_t logger2;
void (*logger_log_sink)(log_message_t) = nullptr;

/// Process-global color enable flag, computed once at startup. Default off:
/// only the daemon (or a test) enables it; lib-only consumers and early
/// startup stay color-free until an explicit decision is made.
static std::atomic<bool> log_color_enabled_flag{false};

void set_log_color_enabled(bool enabled) {
  log_color_enabled_flag.store(enabled, std::memory_order_relaxed);
}

bool log_color_enabled() {
  return log_color_enabled_flag.load(std::memory_order_relaxed);
}

/// The per-thread log tag: set once per thread (e.g. by the actor runtime
/// at thread start); `log()` copies it into every emitted message so the
/// tag travels with the message to the rendering thread.
static thread_local std::string thread_log_tag;

void set_log_thread_tag(std::string name) { thread_log_tag = std::move(name); }

const std::string &current_log_thread_tag() { return thread_log_tag; }

static constexpr const char *ansi_blue() { return "\033[1;34m"; }
static constexpr const char *ansi_yellow() { return "\033[1;33m"; }
static constexpr const char *ansi_color_reset() { return "\033[0m"; }

/// The old per-level color, applied to the whole `level=` token so severity
/// stays scannable at a glance: DEBUG blue, INFO none, WARNING yellow,
/// ERROR red.
static constexpr const char *level_color(logger_level_t level) {
  switch (level) {
  case logger_level_t::DEBUG:
    return "\033[1;34m";
  case logger_level_t::WARNING:
    return "\033[1;33m";
  case logger_level_t::ERROR:
    return "\033[1;31m";
  case logger_level_t::INFO:
  default:
    return "";
  }
}

static bool is_builtin_key(std::string_view key) {
  return key == "thread" || key == "filename";
}

static bool is_key_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

/// Colors every `key=` in a logfmt line: built-in keys blue, all others
/// yellow. Quote-aware: an `=` inside a quoted value is not a key.
static std::string colorize_logfmt(std::string_view line) {
  std::string out;
  out.reserve(line.size() + 48);
  bool in_quote = false;
  const size_t n = line.size();

  for (size_t i = 0; i < n;) {
    const char c = line[i];
    if (in_quote) {
      if (c == '\\' && i + 1 < n) {
        out += c;
        out += line[i + 1];
        i += 2;
      } else if (c == '"') {
        in_quote = false;
        out += c;
        ++i;
      } else {
        out += c;
        ++i;
      }
      continue;
    }
    if (c == '"') {
      in_quote = true;
      out += c;
      ++i;
      continue;
    }
    // Key start: identifier at token start immediately followed by '='.
    if (is_key_char(c) && (i == 0 || line[i - 1] == ' ')) {
      size_t j = i;
      while (j < n && is_key_char(line[j])) {
        ++j;
      }
      if (j < n && line[j] == '=') {
        const std::string_view key = line.substr(i, j - i);
        out += is_builtin_key(key) ? ansi_blue() : ansi_yellow();
        out += key;
        out += ansi_color_reset();
        i = j; // leave the '=' for the next iteration
        continue;
      }
    }
    out += c;
    ++i;
  }
  return out;
}

/// Column where the producer's message (the yellow keys) starts. The
/// built-in prefix (`level= thread= filename=`) is padded to this width so
/// the custom content aligns across lines; longer prefixes overflow without
/// truncation.
static constexpr size_t log_prefix_width = 64;

std::string logger_format_line(const log_message_t &msg) {
  // logfmt: level= thread= filename=<file>:<lineno> <body>, with the prefix
  // padded so the body starts at a fixed column.
  std::string out;
  out.reserve(msg.text.size() + msg.file.size() + msg.thread_name.size() + 48);
  out += "level=";
  out += log_level_name(msg.level);
  out += " thread=";
  out += msg.thread_name;
  out += " filename=";
  out += msg.file;
  out += ':';
  out += std::to_string(msg.lineno);
  if (out.size() < log_prefix_width) {
    out.append(log_prefix_width - out.size(), ' ');
  }
  out += ' ';
  out += msg.text;

  if (!log_color_enabled()) {
    return out;
  }

  // Color the `level=` token by severity (the old per-level colors), then
  // colorize the rest (thread= and filename= blue, custom keys yellow).
  const size_t level_end = out.find(' ');
  std::string result;
  result.reserve(out.size() + 48);
  const char *lc = level_color(msg.level);
  if (lc[0] != '\0') {
    result += lc;
  }
  result.append(out, 0, level_end);
  if (lc[0] != '\0') {
    result += ansi_color_reset();
  }
  result += colorize_logfmt(std::string_view(out).substr(level_end));
  return result;
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
