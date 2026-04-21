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

#include <rtpmidid/dm_json/runtime.hpp>

#include <cctype>
#include <charconv>
#include <cstdio>
#include <string_view>

namespace rtpmididns::dmjson {

parse_error::parse_error(std::string what, std::size_t off, unsigned ln,
                         unsigned col)
    : std::runtime_error(std::move(what)), offset(off), line(ln), column(col) {
}

void writer_t::clear() {
  buf_.clear();
  obj_next_is_first_key_.clear();
  arr_next_is_first_elem_.clear();
}

void writer_t::swap_into_string(std::string &out) {
  out = std::move(buf_);
  buf_.clear();
  obj_next_is_first_key_.clear();
  arr_next_is_first_elem_.clear();
}

void writer_t::begin_object() {
  buf_.push_back('{');
  obj_next_is_first_key_.push_back(true);
}
void writer_t::end_object() {
  if (!obj_next_is_first_key_.empty())
    obj_next_is_first_key_.pop_back();
  buf_.push_back('}');
}
void writer_t::begin_array() {
  buf_.push_back('[');
  arr_next_is_first_elem_.push_back(true);
}
void writer_t::end_array() {
  if (!arr_next_is_first_elem_.empty())
    arr_next_is_first_elem_.pop_back();
  buf_.push_back(']');
}
void writer_t::array_item() {
  if (arr_next_is_first_elem_.empty())
    return;
  if (!arr_next_is_first_elem_.back())
    buf_.push_back(',');
  else
    arr_next_is_first_elem_.back() = false;
}

void writer_t::append_quoted(std::string_view s) {
  buf_.push_back('"');
  for (unsigned char uc : s) {
    switch (static_cast<char>(uc)) {
    case '"':  buf_.append("\\\""); break;
    case '\\': buf_.append("\\\\"); break;
    case '\b': buf_.append("\\b");  break;
    case '\f': buf_.append("\\f");  break;
    case '\n': buf_.append("\\n");  break;
    case '\r': buf_.append("\\r");  break;
    case '\t': buf_.append("\\t");  break;
    default:
      if (uc < 0x20) {
        char esc[8];
        std::snprintf(esc, sizeof(esc), "\\u%04x", uc);
        buf_.append(esc);
      } else {
        buf_.push_back(static_cast<char>(uc));
      }
    }
  }
  buf_.push_back('"');
}

void writer_t::key(std::string_view k) {
  if (obj_next_is_first_key_.empty())
    return;
  if (!obj_next_is_first_key_.back())
    buf_.push_back(',');
  else
    obj_next_is_first_key_.back() = false;
  append_quoted(k);
  buf_.push_back(':');
}

void writer_t::null_value() { buf_.append("null"); }
void writer_t::bool_value(bool v) { buf_.append(v ? "true" : "false"); }

void writer_t::int_value(int64_t v) {
  char tmp[32];
  auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
  if (r.ec != std::errc())
    throw std::runtime_error("int to_chars");
  buf_.append(tmp, static_cast<std::size_t>(r.ptr - tmp));
}

void writer_t::uint_value(uint64_t v) {
  char tmp[32];
  auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
  if (r.ec != std::errc())
    throw std::runtime_error("uint to_chars");
  buf_.append(tmp, static_cast<std::size_t>(r.ptr - tmp));
}

void writer_t::double_value(double v) {
  char tmp[64];
  const int n = std::snprintf(tmp, sizeof(tmp), "%.17g", v);
  if (n < 0 || static_cast<std::size_t>(n) >= sizeof(tmp))
    throw std::runtime_error("double snprintf");
  buf_.append(tmp, static_cast<std::size_t>(n));
}

void writer_t::string_value(std::string_view s) { append_quoted(s); }

void write_ok_array(writer_t &w) {
  w.begin_array();
  w.string_value("ok");
  w.end_array();
}

// --- reader ---

reader_t::reader_t(std::string_view json) : whole_(json) {
  p_ = whole_.data();
  end_ = whole_.data() + whole_.size();
}

[[noreturn]] void reader_t::fail(std::string_view msg) {
  throw parse_error(std::string(msg), cur_offset(), line_, col_);
}

void reader_t::skip_ws() {
  while (p_ < end_) {
    const char c = *p_;
    if (c == ' ' || c == '\t' || c == '\r')
      ++p_, ++col_;
    else if (c == '\n') {
      ++p_;
      ++line_;
      col_ = 1;
    } else
      break;
  }
}

char reader_t::peek() const {
  if (p_ >= end_)
    return 0;
  return *p_;
}

void reader_t::bump() {
  if (p_ >= end_)
    fail("unexpected eof");
  if (*p_ == '\n') {
    ++p_;
    ++line_;
    col_ = 1;
  } else {
    ++p_;
    ++col_;
  }
}

void reader_t::expect_object_begin() {
  skip_ws();
  if (peek() != '{')
    fail("expected '{'");
  bump();
}

void reader_t::expect_object_end() {
  skip_ws();
  if (peek() != '}')
    fail("expected '}'");
  bump();
}

void reader_t::expect_array_begin() {
  skip_ws();
  if (peek() != '[')
    fail("expected '['");
  bump();
}

void reader_t::expect_array_end() {
  skip_ws();
  if (peek() != ']')
    fail("expected ']'");
  bump();
}

void reader_t::expect_colon() {
  skip_ws();
  if (peek() != ':')
    fail("expected ':'");
  bump();
}

void reader_t::expect_comma() {
  skip_ws();
  if (peek() != ',')
    fail("expected ','");
  bump();
}

bool reader_t::try_consume_object_end() {
  skip_ws();
  if (peek() != '}')
    return false;
  bump();
  return true;
}

bool reader_t::try_consume_array_end() {
  skip_ws();
  if (peek() != ']')
    return false;
  bump();
  return true;
}

void reader_t::read_string_into(std::string &out) {
  skip_ws();
  if (peek() != '"')
    fail("expected string");
  bump();
  out.clear();
  while (p_ < end_) {
    char c = *p_;
    bump();
    if (c == '"')
      return;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (p_ >= end_)
      fail("string escape eof");
    char e = *p_;
    bump();
    switch (e) {
    case '"':
      out.push_back('"');
      break;
    case '\\':
      out.push_back('\\');
      break;
    case '/':
      out.push_back('/');
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'u': {
      if (end_ - p_ < 4)
        fail("\\u too short");
      uint32_t cp = 0;
      for (int i = 0; i < 4; ++i) {
        char h = *p_;
        bump();
        cp <<= 4;
        if (h >= '0' && h <= '9')
          cp += static_cast<uint32_t>(h - '0');
        else if (h >= 'a' && h <= 'f')
          cp += static_cast<uint32_t>(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F')
          cp += static_cast<uint32_t>(h - 'A' + 10);
        else
          fail("bad hex in \\u");
      }
      if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
      } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | ((cp >> 6) & 0x1f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
      } else {
        out.push_back(static_cast<char>(0xe0 | ((cp >> 12) & 0x0f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
      }
      break;
    }
    default:
      fail("bad string escape");
    }
  }
  fail("unterminated string");
}

void reader_t::read_string_key_into(std::string &out) { read_string_into(out); }

bool reader_t::read_bool() {
  skip_ws();
  std::string_view rem = remaining();
  if (rem.size() >= 4 && rem.substr(0, 4) == "true") {
    p_ += 4;
    col_ += 4;
    return true;
  }
  if (rem.size() >= 5 && rem.substr(0, 5) == "false") {
    p_ += 5;
    col_ += 5;
    return false;
  }
  fail("expected bool");
}

void reader_t::read_null() {
  skip_ws();
  std::string_view rem = remaining();
  if (rem.size() >= 4 && rem.substr(0, 4) == "null") {
    p_ += 4;
    col_ += 4;
    return;
  }
  fail("expected null");
}

static bool parse_json_number(std::string_view s, double &out_d, int64_t &out_i,
                              uint64_t &out_u, bool &is_int, bool &is_uint,
                              bool &neg) {
  is_int = is_uint = false;
  neg = false;
  if (s.empty())
    return false;
  std::size_t start = 0;
  if (s[0] == '-') {
    neg = true;
    ++start;
  }
  if (start >= s.size())
    return false;
  // Leading zero check (except bare "0")
  if (s[start] == '0' && start + 1 < s.size() && s[start + 1] >= '0' && s[start + 1] <= '9')
    return false;
  // Determine if this is a float (contains '.', 'e', or 'E')
  std::string_view digits = s.substr(start);
  const bool is_float = digits.find('.') != std::string_view::npos ||
                        digits.find('e') != std::string_view::npos ||
                        digits.find('E') != std::string_view::npos;
  const char *b = s.data();
  const char *e = s.data() + s.size();
  if (is_float) {
    auto r = std::from_chars(b, e, out_d);
    if (r.ptr != e || r.ec != std::errc())
      return false;
    return true;
  }
  if (neg) {
    // from_chars for int64_t handles the leading '-' itself
    auto r = std::from_chars(b, e, out_i);
    if (r.ptr != e || r.ec != std::errc())
      return false;
    is_int = true;
    return true;
  }
  auto r = std::from_chars(b, e, out_u);
  if (r.ptr != e || r.ec != std::errc())
    return false;
  is_uint = true;
  return true;
}

void reader_t::skip_value() {
  skip_ws();
  char c = peek();
  if (c == '"') {
    std::string tmp;
    read_string_into(tmp);
    return;
  }
  if (c == '{') {
    bump();
    while (true) {
      skip_ws();
      if (peek() == '}') {
        bump();
        return;
      }
      std::string k;
      read_string_into(k);
      expect_colon();
      skip_value();
      skip_ws();
      if (peek() == '}') {
        bump();
        return;
      }
      expect_comma();
    }
  }
  if (c == '[') {
    bump();
    while (true) {
      skip_ws();
      if (peek() == ']') {
        bump();
        return;
      }
      skip_value();
      skip_ws();
      if (peek() == ']') {
        bump();
        return;
      }
      expect_comma();
    }
  }
  if (c == 't') {
    (void)read_bool();
    return;
  }
  if (c == 'f') {
    (void)read_bool();
    return;
  }
  if (c == 'n') {
    read_null();
    return;
  }
  const char *start = p_;
  if (c == '-')
    bump();
  while (p_ < end_) {
    char d = peek();
    if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' ||
        d == '-')
      bump();
    else
      break;
  }
  if (p_ == start)
    fail("bad value");
}

int64_t reader_t::read_int64() {
  skip_ws();
  const char *start = p_;
  if (peek() == '-')
    bump();
  while (p_ < end_ && peek() >= '0' && peek() <= '9')
    bump();
  std::string_view s(start, static_cast<std::size_t>(p_ - start));
  double d;
  int64_t i;
  uint64_t u;
  bool is_int, is_uint, neg;
  if (!parse_json_number(s, d, i, u, is_int, is_uint, neg))
    fail("bad int");
  if (is_uint && !neg)
    return static_cast<int64_t>(u);
  if (is_int)
    return i;
  return static_cast<int64_t>(d);
}

uint64_t reader_t::read_uint64() {
  skip_ws();
  if (peek() == '-')
    fail("negative as uint");
  const char *start = p_;
  while (p_ < end_ && peek() >= '0' && peek() <= '9')
    bump();
  std::string_view s(start, static_cast<std::size_t>(p_ - start));
  if (s.empty())
    fail("bad uint");
  uint64_t v = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ptr != s.data() + s.size() || r.ec != std::errc())
    fail("bad uint");
  return v;
}

double reader_t::read_double() {
  skip_ws();
  const char *start = p_;
  if (peek() == '-')
    bump();
  while (p_ < end_) {
    char d = peek();
    if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' ||
        d == '-')
      bump();
    else
      break;
  }
  std::string_view s(start, static_cast<std::size_t>(p_ - start));
  double d;
  int64_t i;
  uint64_t u;
  bool is_int, is_uint, neg;
  if (!parse_json_number(s, d, i, u, is_int, is_uint, neg))
    fail("bad double");
  if (is_int)
    return static_cast<double>(i);
  if (is_uint)
    return static_cast<double>(u);
  return d;
}

namespace {

static void trim_inplace(std::string_view &s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
}

} // namespace

namespace rpc {

bool scan_envelope(std::string_view buf, envelope_info_t &out, std::string &err) {
  out = {};
  err.clear();
  trim_inplace(buf);
  if (buf.empty()) {
    err = "empty";
    return false;
  }

  reader_t r(buf);
  try {
    r.expect_object_begin();
    while (true) {
      if (r.try_consume_object_end())
        break;
      std::string key;
      r.read_string_key_into(key);
      r.expect_colon();
      if (key == "method") {
        r.read_string_into(out.method);
      } else if (key == "id") {
        const char *vs = r.remaining().data();
        r.skip_value();
        const char *ve = r.remaining().data();
        out.id_json.assign(vs, static_cast<std::size_t>(ve - vs));
        out.has_id = true;
      } else if (key == "params") {
        const char *vs = r.remaining().data();
        r.skip_value();
        const char *ve = r.remaining().data();
        out.params_json = std::string_view(vs, static_cast<std::size_t>(ve - vs));
        out.has_params = true;
      } else {
        r.skip_value();
      }
      r.skip_ws();
      if (r.try_consume_object_end())
        break;
      r.expect_comma();
    }
    return true;
  } catch (const parse_error &ex) {
    err = ex.what();
    return false;
  }
}

} // namespace rpc

} // namespace rtpmididns::dmjson
