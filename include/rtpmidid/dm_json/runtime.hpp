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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns::dmjson {

struct parse_error : public std::runtime_error {
  std::size_t offset = 0;
  unsigned line = 1;
  unsigned column = 1;

  parse_error(std::string what, std::size_t off, unsigned ln, unsigned col);
};

/** Append-only JSON writer backed by std::string (no manual memory management). */
class writer_t {
  std::string buf_;
  std::vector<bool> obj_next_is_first_key_;
  std::vector<bool> arr_next_is_first_elem_;

  void append_quoted(std::string_view s);

public:
  writer_t() = default;
  writer_t(const writer_t &) = delete;
  writer_t &operator=(const writer_t &) = delete;
  writer_t(writer_t &&) = delete;
  writer_t &operator=(writer_t &&) = delete;

  void clear();
  [[nodiscard]] std::string_view view() const { return buf_; }
  void swap_into_string(std::string &out);

  void raw(std::string_view s) { buf_.append(s); }

  void begin_object();
  void end_object();
  void begin_array();
  void end_array();
  /** Call before each array element (including the first); handles commas. */
  void array_item();

  void key(std::string_view k);
  void null_value();
  void bool_value(bool v);
  void int_value(int64_t v);
  void uint_value(uint64_t v);
  void double_value(double v);
  void string_value(std::string_view s);

  template <typename Int>
  typename std::enable_if<std::is_integral<Int>::value &&
                              !std::is_same<Int, bool>::value,
                          void>::type
  number_value(Int v) {
    if constexpr (std::is_signed<Int>::value)
      int_value(static_cast<int64_t>(v));
    else
      uint_value(static_cast<uint64_t>(v));
  }
};

/**
 * Pull parser over JSON text. After expect_object_begin(), use read_object_key_or_end()
 * then expect_colon(), then read_* for value, then repeat.
 */
class reader_t {
  std::string_view whole_;
  const char *p_ = nullptr;
  const char *end_ = nullptr;
  unsigned line_ = 1;
  unsigned col_ = 1;

  [[noreturn]] void fail(std::string_view msg);
  [[nodiscard]] std::size_t cur_offset() const {
    return static_cast<std::size_t>(p_ - whole_.data());
  }

public:
  explicit reader_t(std::string_view json);
  void skip_ws();
  [[nodiscard]] char peek() const;
  void bump();

  [[nodiscard]] std::string_view remaining() const {
    return std::string_view(p_, static_cast<std::size_t>(end_ - p_));
  }

  void expect_object_begin();
  void expect_object_end();
  void expect_array_begin();
  void expect_array_end();
  void expect_colon();
  void expect_comma();

  /** If next non-ws is `}`, consume it and return true. Else return false. */
  bool try_consume_object_end();
  /** If next non-ws is `]`, consume it and return true. */
  bool try_consume_array_end();

  void read_string_into(std::string &out);

  [[nodiscard]] bool read_bool();
  void read_null();
  [[nodiscard]] int64_t read_int64();
  [[nodiscard]] uint64_t read_uint64();
  [[nodiscard]] double read_double();
  void skip_value();

  /** Read a JSON string token (for object keys). */
  void read_string_key_into(std::string &out);
};

namespace rpc {

struct envelope_info_t {
  std::string method;
  /** Exact JSON text of the `id` value (null, number, or string), for echo in responses. */
  bool has_id = false;
  std::string id_json;
  /** View into original buffer; valid until buffer is modified. */
  std::string_view params_json;
  bool has_params = false;
};

bool scan_envelope(std::string_view buf, envelope_info_t &out, std::string &err);

} // namespace rpc

/** Writes `["ok"]` — legacy control-RPC success shape. */
void write_ok_array(writer_t &w);


} // namespace rtpmididns::dmjson
