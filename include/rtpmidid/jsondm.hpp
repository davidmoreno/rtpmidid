/**
 * json-dm: strict, allocation-friendly JSON (de)serialization for
 * /// [JSON-DM] decorated structs.
 *
 * Generated code (from scripts/json_dm_to_cpp.py) specializes
 * jsondm::serializer<T> / jsondm::deserializer<T> for each decorated struct;
 * this header provides the shared runtime: Writer/Reader, primitives,
 * containers, optional/variant semantics, the fmt adapter, and the public
 * API. All definitions are inline and ODR-safe.
 *
 * Happy path is allocation-free; allocation is permitted only on error paths
 * and when target containers must grow ("avoid, not forbid").
 *
 * Copyright (C) 2025 David Moreno Montero <dmoreno@coralbits.com>
 * GPLv3, same as rtpmidid.
 */
#pragma once

#include "formatterhelper.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jsondm {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

class exception : public std::exception {
  std::string msg_;

public:
  template <typename... Args>
  exception(FMT::format_string<Args...> msg, Args &&...args)
      : msg_(FMT::format(msg, std::forward<Args>(args)...)) {}
  const char *what() const noexcept override { return msg_.c_str(); }
};

namespace detail {
inline std::string hex4(uint16_t v) {
  const char *d = "0123456789abcdef";
  std::string r(6, 'u');
  r[0] = '\\';
  r[1] = 'u';
  r[2] = d[(v >> 12) & 0xF];
  r[3] = d[(v >> 8) & 0xF];
  r[4] = d[(v >> 4) & 0xF];
  r[5] = d[v & 0xF];
  return r;
}
// Append UTF-8 encoding of a Unicode code point.
inline void append_utf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
inline int hexval(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
constexpr size_t max_depth = 512;
constexpr size_t max_path = 32;
} // namespace detail

// ---------------------------------------------------------------------------
// Writer: streaming sink with small inline buffer
// ---------------------------------------------------------------------------

class Writer {
public:
  using flush_fn = void (*)(void *, const char *, size_t);

private:
  char buf_[128];
  size_t pos_ = 0;
  void *ctx_ = nullptr;
  flush_fn flush_ = nullptr;
  int fd_ = -1;
  struct frame {
    bool in_object;
    bool first;
  };
  frame frames_[detail::max_depth];
  size_t frames_n_ = 0;
  struct path_entry {
    std::string_view name;
    size_t idx = 0;
    bool is_idx = false;
  };
  path_entry path_[detail::max_path];
  size_t path_len_ = 0;

public:
  Writer(std::string &out) : ctx_(&out), flush_(string_sink_flush) {}
  Writer(int fd) : ctx_(&fd_), flush_(fd_sink_flush), fd_(fd) {}
  template <class OutIt> explicit Writer(OutIt &out) : ctx_(&out) {
    flush_ = iter_sink_flush<OutIt>;
  }
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  // -- sinks ---------------------------------------------------------------
  static void string_sink_flush(void *ctx, const char *d, size_t n) {
    static_cast<std::string *>(ctx)->append(d, n);
  }
  static void fd_sink_flush(void *ctx, const char *d, size_t n) {
    int fd = *static_cast<int *>(ctx);
    const char *p = d;
    size_t left = n;
    while (left > 0) {
      ssize_t r = ::write(fd, p, left);
      if (r < 0) {
        if (errno == EINTR)
          continue;
        throw exception("JSON serialization error: write() failed: {}",
                        std::string(strerror(errno)));
      }
      p += r;
      left -= static_cast<size_t>(r);
    }
  }
  template <class OutIt>
  static void iter_sink_flush(void *ctx, const char *d, size_t n) {
    auto &it = *static_cast<OutIt *>(ctx);
    it = std::copy_n(d, n, it);
  }
  template <class OutIt> OutIt &sink_ref() {
    return *static_cast<OutIt *>(ctx_);
  }

  // -- appends -------------------------------------------------------------
  void append(char c) {
    size_t p = pos_;
    if (p >= sizeof(buf_)) {
      flush();
      p = 0;
    }
    buf_[p] = c;
    pos_ = p + 1;
  }
  void append(const char *s, size_t n) {
    if (pos_ + n <= sizeof(buf_)) {
      std::memcpy(buf_ + pos_, s, n);
      pos_ += n;
    } else {
      flush();
      if (n >= sizeof(buf_)) {
        flush_(ctx_, s, n);
      } else {
        std::memcpy(buf_, s, n);
        pos_ = n;
      }
    }
  }
  void append(std::string_view s) { append(s.data(), s.size()); }
  void flush() {
    if (pos_ != 0 && flush_ != nullptr) {
      flush_(ctx_, buf_, pos_);
      pos_ = 0;
    }
  }

  // -- structure -----------------------------------------------------------
  void obj_begin() {
    depth_check();
    frames_[frames_n_++] = frame{true, true};
    append('{');
  }
  void obj_end() {
    append('}');
    --frames_n_;
  }
  void arr_begin() {
    depth_check();
    frames_[frames_n_++] = frame{false, true};
    append('[');
  }
  void arr_end() {
    append(']');
    --frames_n_;
  }
  void depth_check() {
    if (frames_n_ >= detail::max_depth) {
      throw exception(
          "JSON serialization error at {}: nesting exceeds {} levels",
          path_str(), detail::max_depth);
    }
  }
  // Emits ',' between members/elements of the current container, unless it
  // is the first one. Called by member_guard.
  void member_sep() {
    if (frames_n_ == 0)
      return;
    auto &f = frames_[frames_n_ - 1];
    if (f.first) {
      f.first = false;
    } else {
      append(',');
    }
  }
  // Writes an escaped JSON key plus ':'.
  void key(std::string_view name) {
    append('"');
    write_escaped(*this, name);
    append("\":", 2);
  }

  // -- path tracking (for error messages) ----------------------------------
  void push(std::string_view name) {
    if (path_len_ < detail::max_path) {
      path_[path_len_++] = path_entry{name, 0, false};
    }
  }
  void push(size_t idx) {
    if (path_len_ < detail::max_path) {
      path_[path_len_++] = path_entry{std::string_view(), idx, true};
    }
  }
  void pop() {
    if (path_len_ != 0)
      --path_len_;
  }
  std::string path_str() const {
    std::string r;
    for (size_t i = 0; i < path_len_; ++i) {
      if (path_[i].is_idx) {
        r += '[';
        r += std::to_string(path_[i].idx);
        r += ']';
      } else {
        if (!r.empty())
          r += '.';
        r.append(path_[i].name);
      }
    }
    return r;
  }

  // Escaped string content (without surrounding quotes).
  static void write_escaped(Writer &w, std::string_view s) {
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      char c = s[i];
      if (c == '"' || c == '\\' || (static_cast<unsigned char>(c) < 0x20)) {
        w.append(s.data() + start, i - start);
        switch (c) {
        case '"':
          w.append("\\\"", 2);
          break;
        case '\\':
          w.append("\\\\", 2);
          break;
        case '\b':
          w.append("\\b", 2);
          break;
        case '\f':
          w.append("\\f", 2);
          break;
        case '\n':
          w.append("\\n", 2);
          break;
        case '\r':
          w.append("\\r", 2);
          break;
        case '\t':
          w.append("\\t", 2);
          break;
        default: {
          auto h = detail::hex4(
              static_cast<uint16_t>(static_cast<unsigned char>(c)));
          w.append(h.data(), h.size());
          break;
        }
        }
        start = i + 1;
      }
    }
    w.append(s.data() + start, s.size() - start);
  }

  // RAII guard used by generated code: writes the key and pushes the path.
  struct member_guard {
    Writer &w;
    member_guard(Writer &w_, std::string_view name) : w(w_) {
      w.member_sep();
      w.key(name);
      w.push(name);
    }
    member_guard(Writer &w_, size_t idx) : w(w_) {
      w.member_sep();
      w.push(idx);
    }
    ~member_guard() { w.pop(); }
  };
};

// ---------------------------------------------------------------------------
// Reader: zero-copy parser over a string_view
// ---------------------------------------------------------------------------

class Reader {
public:
  enum class type { null, boolean, number, string, array, object, invalid };

private:
  std::string_view in_;
  size_t pos_ = 0;
  size_t depth_ = 0;
  std::string_view key_;
  struct frame {
    bool in_object;
    bool first;
  };
  frame frames_[detail::max_depth];
  size_t frames_n_ = 0;
  struct path_entry {
    std::string_view name;
    size_t idx = 0;
    bool is_idx = false;
  };
  path_entry path_[detail::max_path];
  size_t path_len_ = 0;

public:
  explicit Reader(std::string_view in) : in_(in) {}

  size_t offset() const { return pos_; }
  size_t mark() const { return pos_; }
  void restore(size_t m) { pos_ = m; }
  bool at_end() {
    skip_ws();
    return pos_ >= in_.size();
  }

  [[noreturn]] void fail(std::string_view expected,
                         std::string_view found) const {
    const size_t ctx_rad = 20;
    size_t start = pos_ > ctx_rad ? pos_ - ctx_rad : 0;
    size_t len = std::min(in_.size() - start, size_t(2 * ctx_rad));
    std::string near(in_.substr(start, len));
    throw exception(
        "JSON parse error at offset {} ({}): expected {}, found {} — near `{}`",
        pos_, path_str(), expected, std::string(found), near);
  }

  // -- low level -----------------------------------------------------------
  void skip_ws() {
    while (pos_ < in_.size() && (in_[pos_] == ' ' || in_[pos_] == '\t' ||
                                 in_[pos_] == '\n' || in_[pos_] == '\r')) {
      ++pos_;
    }
  }
  char peek() {
    skip_ws();
    return pos_ < in_.size() ? in_[pos_] : '\0';
  }
  void expect(char c, std::string_view what) {
    skip_ws();
    if (pos_ >= in_.size() || in_[pos_] != c) {
      std::string found =
          pos_ < in_.size() ? std::string(1, in_[pos_]) : std::string("<eof>");
      fail(what, found);
    }
    ++pos_;
  }

  // -- containers ----------------------------------------------------------
  void obj_begin() {
    depth_check();
    expect('{', "'{'");
    push_frame(true);
  }
  bool next_key() {
    frame &f = top_frame();
    skip_ws();
    if (f.in_object && pos_ < in_.size() && in_[pos_] == '}')
      return false;
    if (!f.first)
      expect(',', "','");
    f.first = false;
    skip_ws();
    expect('"', "object key");
    key_ = read_raw_string();
    skip_ws();
    expect(':', "':'");
    return true;
  }
  std::string_view key() const { return key_; }
  void obj_end() {
    skip_ws();
    expect('}', "'}'");
    pop_frame();
  }
  void arr_begin() {
    depth_check();
    expect('[', "'['");
    push_frame(false);
  }
  bool next_elem() {
    frame &f = top_frame();
    skip_ws();
    if (!f.in_object && pos_ < in_.size() && in_[pos_] == ']')
      return false;
    if (!f.first)
      expect(',', "','");
    f.first = false;
    return true;
  }
  void arr_end() {
    skip_ws();
    expect(']', "']'");
    pop_frame();
  }
  void depth_check() {
    if (depth_ >= detail::max_depth) {
      fail("value", "nesting too deep");
    }
    ++depth_;
  }
  void push_frame(bool in_object) {
    frames_[frames_n_++] = frame{in_object, true};
  }
  frame &top_frame() { return frames_[frames_n_ - 1]; }
  void pop_frame() { --frames_n_; }

  // -- tokens --------------------------------------------------------------
  type peek_type() {
    char c = peek();
    switch (c) {
    case 'n':
      return type::null;
    case 't':
    case 'f':
      return type::boolean;
    case '"':
      return type::string;
    case '{':
      return type::object;
    case '[':
      return type::array;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return type::number;
    default:
      return type::invalid;
    }
  }
  static const char *type_name(type t) {
    switch (t) {
    case type::null:
      return "null";
    case type::boolean:
      return "boolean";
    case type::number:
      return "number";
    case type::string:
      return "string";
    case type::array:
      return "array";
    case type::object:
      return "object";
    default:
      return "value";
    }
  }

  // Raw (unescaped) string content; caller must have consumed the opening '"'.
  // Does NOT validate escapes; used for keys only.
  std::string_view read_raw_string() {
    size_t start = pos_;
    while (pos_ < in_.size()) {
      char c = in_[pos_++];
      if (c == '"') {
        return in_.substr(start, pos_ - start - 1);
      }
      if (c == '\\') {
        if (pos_ < in_.size())
          ++pos_;
      }
    }
    fail("string termination", "<eof>");
  }

  void read_bool(bool &v) {
    if (peek() != 't' && peek() != 'f') {
      fail("boolean", type_name(peek_type()));
    }
    if (consume_literal("true")) {
      v = true;
      return;
    }
    if (consume_literal("false")) {
      v = false;
      return;
    }
    fail("boolean", "invalid token");
  }
  void read_null() {
    if (!consume_literal("null")) {
      fail("null", type_name(peek_type()));
    }
  }
  bool consume_literal(const char *lit) {
    size_t n = std::strlen(lit);
    skip_ws();
    if (pos_ + n <= in_.size() &&
        in_.substr(pos_, n) == std::string_view(lit, n)) {
      pos_ += n;
      return true;
    }
    return false;
  }

  // Consumes exactly one JSON number token and returns its span.
  std::string_view read_number_token() {
    skip_ws();
    size_t start = pos_;
    if (pos_ < in_.size() && in_[pos_] == '-')
      ++pos_;
    if (pos_ >= in_.size() || !is_digit(in_[pos_])) {
      fail("number", type_name(peek_type()));
    }
    while (pos_ < in_.size() && is_digit(in_[pos_]))
      ++pos_;
    if (pos_ < in_.size() && in_[pos_] == '.') {
      ++pos_;
      if (pos_ >= in_.size() || !is_digit(in_[pos_]))
        fail("number", "malformed fraction");
      while (pos_ < in_.size() && is_digit(in_[pos_]))
        ++pos_;
    }
    if (pos_ < in_.size() && (in_[pos_] == 'e' || in_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < in_.size() && (in_[pos_] == '+' || in_[pos_] == '-'))
        ++pos_;
      if (pos_ >= in_.size() || !is_digit(in_[pos_]))
        fail("number", "malformed exponent");
      while (pos_ < in_.size() && is_digit(in_[pos_]))
        ++pos_;
    }
    return in_.substr(start, pos_ - start);
  }

  template <class Int> void read_int(Int &v) {
    auto token = read_number_token();
    for (char c : token) {
      if (c == '.' || c == 'e' || c == 'E') {
        fail("integer", "number with fraction/exponent");
      }
    }
    auto res = std::from_chars(token.data(), token.data() + token.size(), v);
    if (res.ec == std::errc::result_out_of_range)
      fail("integer", "out of range");
    if (res.ec != std::errc())
      fail("integer", std::string(token));
  }
  void read_float(double &v) {
    auto token = read_number_token();
    auto res = std::from_chars(token.data(), token.data() + token.size(), v);
    if (res.ec == std::errc::result_out_of_range)
      fail("number", "out of range");
    if (res.ec != std::errc())
      fail("number", std::string(token));
  }
  void read_float(float &v) {
    auto token = read_number_token();
    auto res = std::from_chars(token.data(), token.data() + token.size(), v);
    if (res.ec == std::errc::result_out_of_range)
      fail("number", "out of range");
    if (res.ec != std::errc())
      fail("number", std::string(token));
  }

  // Decodes a JSON string into out (may contain escapes).
  void read_string(std::string &out) {
    expect('"', "string");
    out.clear();
    // fast path: no escapes
    size_t end = in_.find('"', pos_);
    size_t esc = in_.find('\\', pos_);
    if (end == std::string_view::npos) {
      fail("string termination", "<eof>");
    }
    if (esc == std::string_view::npos || esc > end) {
      out.append(in_.substr(pos_, end - pos_));
      pos_ = end + 1;
      return;
    }
    while (true) {
      size_t q = in_.find('"', pos_);
      size_t b = in_.find('\\', pos_);
      if (q == std::string_view::npos) {
        fail("string termination", "<eof>");
      }
      if (b == std::string_view::npos || b > q) {
        out.append(in_.substr(pos_, q - pos_));
        pos_ = q + 1;
        return;
      }
      out.append(in_.substr(pos_, b - pos_));
      pos_ = b + 1;
      if (pos_ >= in_.size())
        fail("string termination", "<eof>");
      char e = in_[pos_++];
      switch (e) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t cp = read_hex4();
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          // expect a low surrogate
          if (pos_ + 1 < in_.size() && in_[pos_] == '\\' &&
              in_[pos_ + 1] == 'u') {
            pos_ += 2;
            uint32_t lo = read_hex4();
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              fail("valid surrogate pair", "lone high surrogate");
            }
          } else {
            fail("valid surrogate pair", "lone high surrogate");
          }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          fail("valid surrogate pair", "lone low surrogate");
        }
        detail::append_utf8(out, cp);
        break;
      }
      default:
        fail("valid escape", std::string("\\") + e);
      }
    }
  }
  uint32_t read_hex4() {
    if (pos_ + 4 > in_.size())
      fail("4 hex digits", "<eof>");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      int h = detail::hexval(in_[pos_++]);
      if (h < 0)
        fail("hex digit", std::string(1, in_[pos_ - 1]));
      v = (v << 4) | static_cast<uint32_t>(h);
    }
    return v;
  }

  // Skips any value (used for unknown keys).
  void skip_value() {
    switch (peek_type()) {
    case type::null:
    case type::boolean: {
      // consume the literal by scanning letters
      size_t start = pos_;
      skip_ws();
      start = pos_;
      while (pos_ < in_.size() && (in_[pos_] >= 'a' && in_[pos_] <= 'z'))
        ++pos_;
      std::string_view w(in_.data() + start, pos_ - start);
      if (w != "null" && w != "true" && w != "false") {
        fail("valid literal", std::string(w));
      }
      break;
    }
    case type::number:
      read_number_token();
      break;
    case type::string:
      read_string_skip();
      break;
    case type::array:
      arr_begin();
      while (next_elem())
        skip_value();
      arr_end();
      break;
    case type::object:
      obj_begin();
      while (next_key())
        skip_value();
      obj_end();
      break;
    default:
      fail("value", "<invalid token>");
    }
  }
  // Skips any value and returns the exact span it occupied (used by the
  // control socket boundary to hand raw params to a typed dispatcher).
  std::string_view skip_value_span() {
    skip_ws();
    size_t start = pos_;
    skip_value();
    return in_.substr(start, pos_ - start);
  }
  void read_string_skip() {
    expect('"', "string");
    while (pos_ < in_.size()) {
      char c = in_[pos_++];
      if (c == '"')
        return;
      if (c == '\\') {
        if (pos_ >= in_.size())
          fail("string termination", "<eof>");
        if (in_[pos_] == 'u') {
          ++pos_;
          if (pos_ + 4 > in_.size())
            fail("hex digits", "<eof>");
          pos_ += 4;
        } else {
          ++pos_;
        }
      }
    }
    fail("string termination", "<eof>");
  }

  // -- path tracking -------------------------------------------------------
  void push(std::string_view name) {
    if (path_len_ < detail::max_path)
      path_[path_len_++] = path_entry{name, 0, false};
  }
  void push(size_t idx) {
    if (path_len_ < detail::max_path)
      path_[path_len_++] = path_entry{std::string_view(), idx, true};
  }
  void pop() {
    if (path_len_ != 0)
      --path_len_;
  }
  std::string path_str() const {
    std::string r;
    for (size_t i = 0; i < path_len_; ++i) {
      if (path_[i].is_idx) {
        r += '[';
        r += std::to_string(path_[i].idx);
        r += ']';
      } else {
        if (!r.empty())
          r += '.';
        r.append(path_[i].name);
      }
    }
    return r;
  }

  // RAII guard used by generated code: pushes the path for a member.
  struct member_guard {
    Reader &r;
    member_guard(Reader &r_, std::string_view name) : r(r_) { r.push(name); }
    member_guard(Reader &r_, size_t idx) : r(r_) { r.push(idx); }
    ~member_guard() { r.pop(); }
  };

private:
  static bool is_digit(char c) { return c >= '0' && c <= '9'; }
};

// ---------------------------------------------------------------------------
// Type classification (for variant dispatch)
// ---------------------------------------------------------------------------

enum class json_type { null, boolean, number, string, array, object };

template <class T, class = void> struct json_type_of {
  static constexpr json_type value = json_type::object; // user struct
};
template <> struct json_type_of<std::monostate> {
  static constexpr json_type value = json_type::null;
};
template <> struct json_type_of<bool> {
  static constexpr json_type value = json_type::boolean;
};
template <class T>
struct json_type_of<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
  static constexpr json_type value = json_type::number;
};
template <> struct json_type_of<float> {
  static constexpr json_type value = json_type::number;
};
template <> struct json_type_of<double> {
  static constexpr json_type value = json_type::number;
};
template <> struct json_type_of<std::string> {
  static constexpr json_type value = json_type::string;
};
template <> struct json_type_of<std::string_view> {
  static constexpr json_type value = json_type::string;
};
template <class T, class A> struct json_type_of<std::vector<T, A>> {
  static constexpr json_type value = json_type::array;
};
template <class K, class V, class H, class E, class A>
struct json_type_of<std::unordered_map<K, V, H, E, A>> {
  static constexpr json_type value = json_type::object;
};
template <class T> struct json_type_of<std::optional<T>> {
  static constexpr json_type value = json_type_of<T>::value;
};

// ---------------------------------------------------------------------------
// serializer / deserializer: primary templates, specialized by generated code
// ---------------------------------------------------------------------------

template <typename T> struct serializer;
template <typename T> struct deserializer;
// INI backends: specialized by generated code for /// [INI-DM] structs.
template <typename T> struct ini_serializer;
template <typename T> struct ini_deserializer;

// ---------------------------------------------------------------------------
// fmt adapter base
// ---------------------------------------------------------------------------

template <typename T> struct formatter_base {
  constexpr auto parse(FMT::format_parse_context &ctx) { return ctx.begin(); }
  auto format(const T &v, FMT::format_context &ctx) const {
    auto it = ctx.out();
    Writer w(it);
    serializer<T>::write(v, w);
    w.flush();
    return w.sink_ref<decltype(it)>();
  }
};

// ---------------------------------------------------------------------------
// Builtin-type detection: keeps the user-type primary template from competing
// with the primitive/container overloads.
// ---------------------------------------------------------------------------

template <class T> struct is_vector : std::false_type {};
template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};
template <class T> struct is_map : std::false_type {};
template <class K, class V, class H, class E, class A>
struct is_map<std::unordered_map<K, V, H, E, A>> : std::true_type {};
template <class T> struct is_optional : std::false_type {};
template <class T> struct is_optional<std::optional<T>> : std::true_type {};
template <class T> struct is_variant : std::false_type {};
template <class... Ts>
struct is_variant<std::variant<Ts...>> : std::true_type {};

template <class T>
inline constexpr bool is_json_builtin_v =
    std::is_arithmetic_v<std::remove_cv_t<T>> ||
    std::is_same_v<std::remove_cv_t<T>, std::string> ||
    std::is_same_v<std::remove_cv_t<T>, std::string_view> ||
    std::is_same_v<std::remove_cv_t<T>, std::monostate> ||
    is_vector<std::remove_cv_t<T>>::value ||
    is_map<std::remove_cv_t<T>>::value ||
    is_optional<std::remove_cv_t<T>>::value ||
    is_variant<std::remove_cv_t<T>>::value;

// ---------------------------------------------------------------------------
// write(): serialize primitives / containers / user types into a Writer
// ---------------------------------------------------------------------------

inline void write(Writer &w, bool v) {
  w.append(v ? "true" : "false", v ? 4 : 5);
}
inline void write(Writer &w, std::monostate) { w.append("null", 4); }
inline void write(Writer &w, std::string_view v) {
  w.append('"');
  Writer::write_escaped(w, v);
  w.append('"');
}
inline void write(Writer &w, const std::string &v) {
  write(w, std::string_view(v));
}
template <class T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                           int> = 0>
inline void write(Writer &w, T v) {
  char buf[24];
  auto res = std::to_chars(buf, buf + sizeof(buf), v);
  w.append(buf, static_cast<size_t>(res.ptr - buf));
}
inline void write_float_checked(Writer &w, double v) {
  if (std::isnan(v) || std::isinf(v)) {
    throw exception(
        "JSON serialization error at {}: value {} is not representable in JSON",
        w.path_str(), v);
  }
  char buf[32];
  auto res = std::to_chars(buf, buf + sizeof(buf), v);
  w.append(buf, static_cast<size_t>(res.ptr - buf));
}
inline void write(Writer &w, double v) { write_float_checked(w, v); }
inline void write(Writer &w, float v) {
  write_float_checked(w, static_cast<double>(v));
}

template <class T, class A>
inline void write(Writer &w, const std::vector<T, A> &v) {
  w.arr_begin();
  for (size_t i = 0; i < v.size(); ++i) {
    Writer::member_guard g(w, i);
    write(w, v[i]);
  }
  w.arr_end();
}
template <class V, class H, class E, class A>
inline void write(Writer &w,
                  const std::unordered_map<std::string, V, H, E, A> &v) {
  w.obj_begin();
  for (const auto &kv : v) {
    Writer::member_guard g(w, kv.first);
    write(w, kv.second);
  }
  w.obj_end();
}
template <class T> inline void write(Writer &w, const std::optional<T> &v) {
  if (v.has_value()) {
    write(w, *v);
  } else {
    w.append("null", 4);
  }
}
template <class... Ts>
inline void write(Writer &w, const std::variant<Ts...> &v) {
  std::visit([&](const auto &alt) { write(w, alt); }, v);
}

// user types: primary template forwards to the generated specialization
// (excluded for builtins so it never competes with the overloads above)
template <class T>
inline std::enable_if_t<!is_json_builtin_v<T>> write(Writer &w, const T &v) {
  serializer<T>::write(v, w);
}

// ---------------------------------------------------------------------------
// read(): deserialize into primitives / containers / user types
// ---------------------------------------------------------------------------

inline void read(Reader &r, bool &v) { r.read_bool(v); }
inline void read(Reader &r, std::string &v) { r.read_string(v); }
template <class T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                           int> = 0>
inline void read(Reader &r, T &v) {
  r.read_int(v);
}
inline void read(Reader &r, double &v) { r.read_float(v); }
inline void read(Reader &r, float &v) { r.read_float(v); }

template <class T, class A> inline void read(Reader &r, std::vector<T, A> &v) {
  r.arr_begin();
  size_t i = 0;
  while (r.next_elem()) {
    Reader::member_guard g(r, i);
    if (i < v.size()) {
      if constexpr (std::is_same_v<T, bool>) {
        bool b;
        read(r, b);
        v[i] = b;
      } else {
        read(r, v[i]);
      }
    } else {
      T elem{};
      read(r, elem);
      v.push_back(std::move(elem));
    }
    ++i;
  }
  r.arr_end();
  v.resize(i);
}
template <class V, class H, class E, class A>
inline void read(Reader &r, std::unordered_map<std::string, V, H, E, A> &v) {
  r.obj_begin();
  v.clear();
  while (r.next_key()) {
    auto key = r.key();
    Reader::member_guard g(r, key);
    auto &slot = v[std::string(key)];
    read(r, slot);
  }
  r.obj_end();
}
template <class T> inline void read(Reader &r, std::optional<T> &v) {
  if (r.peek_type() == Reader::type::null) {
    r.skip_value();
    v.reset();
  } else if (v.has_value()) {
    read(r, *v);
  } else {
    T t{};
    read(r, t);
    v = std::move(t);
  }
}

namespace detail {
template <class Variant, size_t... Is>
constexpr size_t count_matches(Reader::type t, std::index_sequence<Is...>) {
  return (0 + ... +
          (json_type_of<std::variant_alternative_t<Is, Variant>>::value ==
           static_cast<json_type>(t)));
}
template <class Variant, size_t I = 0>
void dispatch_into(Reader &r, Variant &v, Reader::type t) {
  if constexpr (I < std::variant_size_v<Variant>) {
    using alt = std::variant_alternative_t<I, Variant>;
    if constexpr (json_type_of<alt>::value == json_type::null) {
      // null alternatives are handled before dispatch; never match here
      dispatch_into<Variant, I + 1>(r, v, t);
    } else {
      if (json_type_of<alt>::value == static_cast<json_type>(t)) {
        v.template emplace<I>();
        read(r, std::get<I>(v));
      } else {
        dispatch_into<Variant, I + 1>(r, v, t);
      }
    }
  }
}
} // namespace detail

template <class... Ts> inline void read(Reader &r, std::variant<Ts...> &v) {
  using var_t = std::variant<Ts...>;
  auto t = r.peek_type();
  constexpr bool has_monostate = (std::is_same_v<Ts, std::monostate> || ...);
  if (t == Reader::type::null) {
    if constexpr (has_monostate) {
      r.skip_value();
      v = std::monostate{};
      return;
    } else {
      r.fail(Reader::type_name(Reader::type::null),
             "no monostate alternative in variant");
    }
  }
  constexpr size_t n = sizeof...(Ts);
  auto seq = std::make_index_sequence<n>{};
  const size_t matches = detail::count_matches<var_t>(t, seq);
  if (matches == 0) {
    r.fail(Reader::type_name(t), "no matching variant alternative");
  }
  if (matches > 1) {
    r.fail("a unique variant alternative",
           "multiple alternatives match this JSON type");
  }
  detail::dispatch_into<var_t>(r, v, t);
}

// user types: primary template forwards to the generated specialization
// (excluded for builtins so it never competes with the overloads above)
template <class T>
inline std::enable_if_t<!is_json_builtin_v<T>> read(Reader &r, T &v) {
  deserializer<T>::read(r, v);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Appends the JSON representation of v to out.
template <class T> inline void serialize(const T &v, std::string &out) {
  Writer w(out);
  write(w, v);
  w.flush();
}
// Streams the JSON representation of v to a file descriptor.
template <class T> inline void serialize(const T &v, int fd) {
  Writer w(fd);
  write(w, v);
  w.flush();
}
// Parses JSON from in into v. Throws jsondm::exception on any error.
template <class T> inline void deserialize(std::string_view in, T &v) {
  Reader r(in);
  read(r, v);
  if (!r.at_end()) {
    r.fail("end of input", "trailing data");
  }
}

} // namespace jsondm

// ---------------------------------------------------------------------------
// INI support (config files). INI values are strings; generated INI code
// converts them with jsondm::ini::to_value<T> (read) / to_text<T> (write).
// ---------------------------------------------------------------------------

namespace jsondm {
namespace ini {

template <class> inline constexpr bool always_false = false;

namespace detail {
template <class T, class = void> struct ini_is_arithmetic : std::false_type {};
template <class T>
struct ini_is_arithmetic<
    T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
    : std::true_type {};
} // namespace detail

// Read conversion: string value -> member. Specialize for config-specific
// types (std::regex, enums, ...) outside this header.
template <class T> inline T to_value(std::string_view value) {
  if constexpr (detail::ini_is_arithmetic<T>::value) {
    T out{};
    auto r = std::from_chars(value.data(), value.data() + value.size(), out);
    if (r.ec != std::errc() || r.ptr != value.data() + value.size()) {
      throw exception("Invalid number value: {}", std::string(value));
    }
    return out;
  } else {
    static_assert(always_false<T>,
                  "no jsondm::ini::to_value specialization for this type");
  }
}
template <> inline std::string to_value<std::string>(std::string_view v) {
  return std::string(v);
}
template <> inline bool to_value<bool>(std::string_view v) {
  return v == "true";
}

// Write conversion: member -> string value. Specialize similarly.
template <class T> inline std::string to_text(const T &v) {
  if constexpr (detail::ini_is_arithmetic<T>::value) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
  } else {
    static_assert(always_false<T>,
                  "no jsondm::ini::to_text specialization for this type");
  }
}
template <> inline std::string to_text<std::string>(const std::string &v) {
  return v;
}
template <> inline std::string to_text<bool>(const bool &v) {
  return v ? "true" : "false";
}

} // namespace ini

// ---------------------------------------------------------------------------
// IniWriter / IniReader: minimal INI dialect (sections, key=value, '#'
// comments, repeated sections).
// ---------------------------------------------------------------------------

class IniWriter {
public:
  explicit IniWriter(std::string &out) : out_(out) {}
  void section(std::string_view name) {
    out_ += '[';
    out_.append(name);
    out_ += "]\n";
  }
  void key(std::string_view k, std::string_view v) {
    out_.append(k);
    out_ += '=';
    out_.append(v);
    out_ += '\n';
  }

private:
  std::string &out_;
};

class IniReader {
public:
  struct entry {
    std::string_view key;
    std::string_view value;
    size_t line = 0;
  };
  // One occurrence of a section: the entries between two section headers.
  class Section {
  public:
    template <class F> void for_each(F &&f) const {
      for (size_t i = begin_; i < end_; ++i) {
        const auto &e = r_->entries_[i];
        f(e.key, e.value, e.line);
      }
    }
    size_t line() const {
      return begin_ < end_ ? r_->entries_[begin_].line : header_line_;
    }
    std::string_view filename() const { return r_->filename_; }

  private:
    friend class IniReader;
    const IniReader *r_ = nullptr;
    size_t begin_ = 0;
    size_t end_ = 0;
    size_t header_line_ = 0;
  };

  explicit IniReader(std::string_view text, std::string_view filename = {})
      : filename_(filename) {
    parse(text);
  }
  std::string_view filename() const { return filename_; }

  // One pass per occurrence of the named section.
  template <class F> void for_each_section(std::string_view name, F &&f) const {
    for (const auto &o : occurrences_) {
      if (o.section == name) {
        f(section_for(o));
      }
    }
  }
  // One pass per section occurrence, in file order.
  template <class F> void for_each_run(F &&f) const {
    for (const auto &o : occurrences_) {
      f(o.section, section_for(o));
    }
  }

private:
  struct occurrence {
    std::string_view section;
    size_t begin = 0;
    size_t end = 0;
    size_t header_line = 0;
  };
  Section section_for(const occurrence &o) const {
    Section s;
    s.r_ = this;
    s.begin_ = o.begin;
    s.end_ = o.end;
    s.header_line_ = o.header_line;
    return s;
  }
  static std::string_view trim(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
      ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
      --e;
    return s.substr(b, e - b);
  }
  void parse(std::string_view text) {
    size_t pos = 0;
    size_t line_no = 0;
    occurrence current;
    bool have_section = false;
    while (pos <= text.size()) {
      size_t nl = text.find('\n', pos);
      std::string_view line = text.substr(
          pos, (nl == std::string_view::npos ? text.size() : nl) - pos);
      pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
      ++line_no;
      auto hash = line.find('#');
      if (hash != std::string_view::npos)
        line = line.substr(0, hash);
      line = trim(line);
      if (line.empty())
        continue;
      if (line.front() == '[') {
        if (line.back() != ']') {
          throw exception("{}:{}: Invalid section: {}", filename_, line_no,
                          std::string(line));
        }
        if (have_section) {
          current.end = entries_.size();
          occurrences_.push_back(current);
        }
        current = occurrence{line.substr(1, line.size() - 2), entries_.size(),
                             entries_.size(), line_no};
        have_section = true;
      } else {
        auto eq = line.find('=');
        if (eq == std::string_view::npos) {
          throw exception("{}:{}: Invalid line: {}", filename_, line_no,
                          std::string(line));
        }
        auto key = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        entries_.push_back(entry{key, value, line_no});
      }
    }
    if (have_section) {
      current.end = entries_.size();
      occurrences_.push_back(current);
    }
  }
  std::string_view filename_;
  std::vector<entry> entries_;
  std::vector<occurrence> occurrences_;
};

// Fills {{name}} placeholders in raw config text (before INI parsing).
inline std::string fill_template(std::string_view text, std::string_view name,
                                 std::string_view value) {
  std::string needle = "{{";
  needle += name;
  needle += "}}";
  std::string out;
  out.reserve(text.size() + value.size());
  size_t start = 0;
  while (true) {
    size_t at = text.find(needle, start);
    if (at == std::string_view::npos) {
      out.append(text.substr(start));
      break;
    }
    out.append(text.substr(start, at - start));
    out.append(value);
    start = at + needle.size();
  }
  return out;
}
// Fills {{hostname}} with the machine hostname.
inline std::string fill_hostname(std::string_view text) {
  char hostname[256];
  if (::gethostname(hostname, sizeof(hostname)) != 0) {
    std::strncpy(hostname, "localhost", sizeof(hostname) - 1);
  }
  return fill_template(text, "hostname", hostname);
}

} // namespace jsondm
