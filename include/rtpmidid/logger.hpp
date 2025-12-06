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

#pragma once
#include "formatterhelper.hpp"
#include <array>
#include <iostream>
#include <string>

namespace rtpmidid {
enum logger_level_t { DEBUG, INFO, WARNING, ERROR };
}

ENUM_FORMATTER_BEGIN(rtpmidid::logger_level_t);
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::DEBUG, "DEBUG");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::INFO, "INFO ");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::WARNING, "WARN ");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::ERROR, "ERROR");
ENUM_FORMATTER_END();

namespace rtpmidid {

class logger_t {
  using buffer_t = std::array<char, 1024>;
  // we use a preallocated array to avoid any allocation on debug
  buffer_t buffer;
  logger_level_t current_log_level = logger_level_t::INFO;

public:
  buffer_t::iterator log_preamble(logger_level_t level, const char *filename,
                                  int lineno);
  void log_postamble(buffer_t::iterator it);
  void set_log_level(logger_level_t level) { current_log_level = level; }

  template <typename... Args>
  constexpr void log(logger_level_t level, const char *filename, int lineno,
                     FMT::format_string<Args...> message, Args... args) {
    // Runtime filtering: only log if level is at or above current_log_level
    if (level < current_log_level) {
      return;
    }

    auto it = log_preamble(level, filename, lineno);

    auto max_size = buffer.size() - (it - buffer.begin()) - 16;
    auto res =
        FMT::format_to_n(it, max_size, message, std::forward<Args>(args)...);
    it = res.out;

    log_postamble(it);
  }
};
} // namespace rtpmidid

namespace rtpmidid {
extern rtpmidid::logger_t logger2;

// Convert string to logger level (case-insensitive, accepts "debug"/"info"/"warning"/"error" or "0"/"1"/"2"/"3")
logger_level_t str_to_log_level(const std::string &value);
};

/// Compatibility with c++23, std::print, std::println
#if __cplusplus < 202302L
namespace std {
template <typename... Args>
void print(FMT::format_string<Args...> format, Args... args) {
  std::cout << FMT::format(format, std::forward<Args>(args)...);
}

template <typename... Args>
void println(FMT::format_string<Args...> format, Args... args) {
  std::cout << FMT::format(format, std::forward<Args>(args)...) << std::endl;
}
#endif
} // namespace std

#ifdef DEBUG
#undef DEBUG
#endif
#ifdef INFO
#undef INFO
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARNING
#undef WARNING
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL 1 // 1: debug, 2: info, 3: warning, 4: error
#endif

#if LOG_LEVEL <= 1
#define DEBUG(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::DEBUG, __FILE__, __LINE__, \
                          __VA_ARGS__)
#else
#define DEBUG(...)
#endif
#if LOG_LEVEL <= 2
#define INFO(...)                                                              \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::INFO, __FILE__, __LINE__,  \
                          __VA_ARGS__)
#else
#define INFO(...)
#endif
#if LOG_LEVEL <= 3
#define WARNING(...)                                                           \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::WARNING, __FILE__,         \
                          __LINE__, __VA_ARGS__)
#else
#define WARNING(...)
#endif
#if LOG_LEVEL <= 4
#define ERROR(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::ERROR, __FILE__, __LINE__, \
                          __VA_ARGS__)
#else
#define ERROR(...)
#endif

#define WARNING_RATE_LIMIT(seconds, ...)                                       \
  {                                                                            \
    static int __warning_skip_until_##__LINENO__ = 0;                          \
    int __now = time(nullptr);                                                 \
    if (__warning_skip_until_##__LINENO__ < __now) {                           \
      __warning_skip_until_##__LINENO__ = __now + seconds;                     \
      WARNING(__VA_ARGS__);                                                    \
    }                                                                          \
  }

#define ERROR_ONCE(...)                                                        \
  {                                                                            \
    static bool __error_once_unseen_##__LINENO__ = true;                       \
    if (__error_once_unseen_##__LINENO__) {                                    \
      __error_once_unseen_##__LINENO__ = false;                                \
      ERROR(__VA_ARGS__);                                                      \
    }                                                                          \
  }

#define WARNING_ONCE(...)                                                      \
  {                                                                            \
    static bool __warning_once_unseen_##__LINENO__ = true;                     \
    if (__warning_once_unseen_##__LINENO__) {                                  \
      __warning_once_unseen_##__LINENO__ = false;                              \
      WARNING(__VA_ARGS__);                                                    \
    }                                                                          \
  }
