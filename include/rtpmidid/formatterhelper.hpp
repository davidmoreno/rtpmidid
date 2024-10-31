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
#include <format>

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
