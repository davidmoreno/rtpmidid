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
#include <array>
#include <format>
#include <iostream>

namespace rtpmidid {
enum logger_level_t { DEBUG, INFO, WARNING, ERROR };

class logger_t {
  using buffer_t = std::array<char, 1024>;
  buffer_t buffer;

public:
  template <typename It> constexpr It ansi_color(It it, logger_level_t level) {
    switch (level) {
    case DEBUG:
      return std::format_to(it, "\033[1;34m");
    case INFO:
      return std::format_to(it, "\033[1;32m");
    case WARNING:
      return std::format_to(it, "\033[1;33m");
    case ERROR:
      return std::format_to(it, "\033[1;31m");
    default:
      return it;
    }
  }

  template <typename It> constexpr It ansi_color_reset(It it) {
    return std::format_to(it, "\033[0m");
  }

  template <typename... Args>
  constexpr void log(logger_level_t level, const char *filename, int lineno,
                     std::format_string<Args...> message, Args... args) {
    auto it = buffer.begin();
    it = ansi_color(it, level);
    it = std::format_to(it, "[{}] source={}:{} ", level, filename, lineno);
    it = std::format_to(it, message, std::forward<Args>(args)...);
    it = ansi_color_reset(it);
    *it = '\0';
    std::cout << buffer.data() << std::endl;
  }
};
} // namespace rtpmidid

#define BASIC_FORMATTER(T, FMT, ...)                                           \
  template <> struct std::formatter<T> {                                       \
    constexpr auto parse(std::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const T &v, std::format_context &ctx) const {                  \
      return std::format_to(ctx.out(), FMT, __VA_ARGS__);                      \
    }                                                                          \
  }

#define ENUM_FORMATTER_BEGIN(EnumType)                                         \
  template <> struct std::formatter<EnumType> {                                \
    constexpr auto parse(std::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const EnumType &v, std::format_context &ctx) const {           \
      switch (v) {

#define ENUM_FORMATTER_ELEMENT(EnumValue, Str)                                 \
  case EnumValue:                                                              \
    return std::format_to(ctx.out(), Str);

#define ENUM_FORMATTER_DEFAULT()                                               \
  default:                                                                     \
    return std::format_to(ctx.out(), "Unknown");

#define ENUM_FORMATTER_END()                                                   \
  }                                                                            \
  return std::format_to(ctx.out(), "Unknown");                                 \
  }                                                                            \
  }

#define VECTOR_FORMATTER(T)                                                    \
  template <> struct std::formatter<std::vector<T>> {                          \
    constexpr auto parse(std::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const std::vector<T> &v, std::format_context &ctx) const {     \
      auto it = format_to(ctx.out(), "[");                                     \
      for (auto &item : v) {                                                   \
        format_to(it, "{}", item);                                             \
        if (&item != &v.back()) {                                              \
          format_to(it, ", ");                                                 \
        }                                                                      \
      }                                                                        \
      format_to(it, "]");                                                      \
      return it;                                                               \
    }                                                                          \
  }

ENUM_FORMATTER_BEGIN(rtpmidid::logger_level_t);
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::DEBUG, "DEBUG");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::INFO, "INFO");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::WARNING, "WARNING");
ENUM_FORMATTER_ELEMENT(rtpmidid::logger_level_t::ERROR, "ERROR");
ENUM_FORMATTER_END();

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

#define DEBUG(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::DEBUG, __FILE__, __LINE__, \
                          __VA_ARGS__)
#define INFO(...)                                                              \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::INFO, __FILE__, __LINE__,  \
                          __VA_ARGS__)
#define ERROR(...)                                                             \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::ERROR, __FILE__, __LINE__, \
                          __VA_ARGS__)
#define WARNING(...)                                                           \
  ::rtpmidid::logger2.log(rtpmidid::logger_level_t::WARNING, __FILE__,         \
                          __LINE__, __VA_ARGS__)

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

namespace rtpmidid {
extern rtpmidid::logger_t logger2;
};

/// Compatibility with c++23, std::print, std::println
#if __cplusplus < 202302L
namespace std {
template <typename... Args>
void print(std::format_string<Args...> fmt, Args... args) {
  std::cout << std::format(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void println(std::format_string<Args...> fmt, Args... args) {
  std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}
#endif
} // namespace std