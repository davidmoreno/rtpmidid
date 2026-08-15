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

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#ifdef USE_LIBFMT
#define FMT fmt
#include <fmt/core.h>
#else
#define FMT std
#include <format>
#endif

#define BASIC_FORMATTER(T, FORMAT, ...)                                        \
  template <> struct FMT::formatter<T> {                                       \
    constexpr auto parse(FMT::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const T &v, FMT::format_context &ctx) const {                  \
      return FMT::format_to(ctx.out(), FORMAT, __VA_ARGS__);                   \
    }                                                                          \
  }

#define ENUM_FORMATTER_BEGIN(EnumType)                                         \
  template <> struct FMT::formatter<EnumType> {                                \
    constexpr auto parse(FMT::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const EnumType &v, FMT::format_context &ctx) const {           \
      switch (v) {

#define ENUM_FORMATTER_ELEMENT(EnumValue, Str)                                 \
  case EnumValue:                                                              \
    return FMT::format_to(ctx.out(), Str);

#define ENUM_FORMATTER_DEFAULT()                                               \
  default:                                                                     \
    return FMT::format_to(ctx.out(), "Unknown");

#define ENUM_FORMATTER_END()                                                   \
  }                                                                            \
  return FMT::format_to(ctx.out(), "Unknown");                                 \
  }                                                                            \
  }

#define VECTOR_FORMATTER(T)                                                    \
  template <> struct FMT::formatter<std::vector<T>> {                          \
    constexpr auto parse(FMT::format_parse_context &ctx) {                     \
      return ctx.begin();                                                      \
    }                                                                          \
    auto format(const std::vector<T> &v, FMT::format_context &ctx) const {     \
      auto it = ::FMT::format_to(ctx.out(), "[");                              \
      for (auto &item : v) {                                                   \
        ::FMT::format_to(it, "{}", item);                                      \
        if (&item != &v.back()) {                                              \
          ::FMT::format_to(it, ", ");                                          \
        }                                                                      \
      }                                                                        \
      ::FMT::format_to(it, "]");                                               \
      return it;                                                               \
    }                                                                          \
  }

BASIC_FORMATTER(size_t, "{}", (uint32_t)v);
BASIC_FORMATTER(ssize_t, "{}", (int32_t)v);

namespace rtpmidid {

/// logfmt value wrapper: when formatted, emits the value verbatim if it
/// contains no space, `=`, or `"`; otherwise wraps it in double quotes,
/// escaping `\`, `"`, and control chars (newline/tab/carriage-return) so the
/// value stays on one line. Holds a `std::string_view` (no copy); only valid
/// as an immediate format argument, never stored.
struct quoted_t {
  std::string_view value;
  explicit quoted_t(std::string_view v) : value(v) {}
};

} // namespace rtpmidid

// Make the logfmt value helper usable unqualified from any namespace (the
// logging macros are global, but call sites live in rtpmididns/rtpmidid).
using rtpmidid::quoted_t;

template <> struct FMT::formatter<rtpmidid::quoted_t> {
  constexpr auto parse(FMT::format_parse_context &ctx) { return ctx.begin(); }
  auto format(const rtpmidid::quoted_t &q, FMT::format_context &ctx) const {
    const auto &v = q.value;
    bool needs_quote = false;
    for (char c : v) {
      if (c == ' ' || c == '=' || c == '"' || c < ' ') {
        needs_quote = true;
        break;
      }
    }
    if (!needs_quote) {
      return FMT::format_to(ctx.out(), "{}", v);
    }
    auto it = FMT::format_to(ctx.out(), "\"");
    for (char c : v) {
      switch (c) {
      case '\\':
        it = FMT::format_to(it, "\\\\");
        break;
      case '"':
        it = FMT::format_to(it, "\\\"");
        break;
      case '\n':
        it = FMT::format_to(it, "\\n");
        break;
      case '\t':
        it = FMT::format_to(it, "\\t");
        break;
      case '\r':
        it = FMT::format_to(it, "\\r");
        break;
      default:
        it = FMT::format_to(it, "{}", c);
        break;
      }
    }
    return FMT::format_to(it, "\"");
  }
};
