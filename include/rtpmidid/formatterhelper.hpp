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

#include <cstdint>
#include <cstring>

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
