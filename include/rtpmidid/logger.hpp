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

#pragma once
#include <fmt/format.h>
#include <time.h>

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED true
#endif

#if DEBUG_ENABLED
#define DEBUG(...) logger::log(__FILE__, __LINE__, logger::DEBUG, __VA_ARGS__)
#else
#define DEBUG(...) false
#endif
#define WARNING(...)                                                           \
  logger::log(__FILE__, __LINE__, logger::WARNING, __VA_ARGS__)
#define ERROR(...) logger::log(__FILE__, __LINE__, logger::ERROR, __VA_ARGS__)
#define INFO(...) logger::log(__FILE__, __LINE__, logger::INFO, __VA_ARGS__)
#define SUCCESS(...)                                                           \
  logger::log(__FILE__, __LINE__, logger::SUCCESS, __VA_ARGS__)

#define ERROR_ONCE(...)                                                        \
  {                                                                            \
    static bool __error_once_unseen_##__LINENO__ = true;                       \
    if (__error_once_unseen_##__LINENO__) {                                    \
      __error_once_unseen_##__LINENO__ = false;                                \
      logger::log(__FILE__, __LINE__, logger::ERROR, __VA_ARGS__);             \
    }                                                                          \
  }
#define WARNING_ONCE(...)                                                      \
  {                                                                            \
    static bool __warning_once_unseen_##__LINENO__ = true;                     \
    if (__warning_once_unseen_##__LINENO__) {                                  \
      __warning_once_unseen_##__LINENO__ = false;                              \
      logger::log(__FILE__, __LINE__, logger::WARNING, __VA_ARGS__);           \
    }                                                                          \
  }
// Will show only once every X seconds
#define WARNING_RATE_LIMIT(seconds, ...)                                       \
  {                                                                            \
    static int __warning_skip_until_##__LINENO__ = 0;                          \
    int __now = time(nullptr);                                                 \
    if (__warning_skip_until_##__LINENO__ < __now) {                           \
      __warning_skip_until_##__LINENO__ = __now + seconds;                     \
      logger::log(__FILE__, __LINE__, logger::WARNING, __VA_ARGS__);           \
    }                                                                          \
  }

namespace logger {
class logger;

extern logger __logger;

enum LogLevel {
  DEBUG,
  WARNING,
  ERROR,
  INFO,
  SUCCESS,
};

class logger {
private:
  bool is_a_terminal;

public:
  logger();
  ~logger();

  void log(const char *filename, int lineno, LogLevel loglevel,
           const char *msg);
  void flush();
};

template <typename... Args>
inline void log(const char *fullpath, int lineno, LogLevel loglevel,
                Args... args) {
  static char buffer[512];

  // Get ony the file name part, not full path. Assumes a / and ends in 0.
  const char *filename = fullpath;
  while (*filename)
    ++filename;
  while (*filename != '/')
    --filename;
  ++filename;

  auto n = fmt::format_to_n(buffer, sizeof(buffer), args...);
  *n.out = '\0';

  __logger.log(filename, lineno, loglevel, buffer);
}

inline void flush() { __logger.flush(); }
} // namespace logger
