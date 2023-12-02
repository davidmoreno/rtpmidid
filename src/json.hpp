/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtpmididns {
// we use the low 4 bits of the value to store the type
// this means we can store 2^60 values of each type
// we use the high 60 bits to store the value
// we may lose some precission, but not important for use case

#define TAG_SHIFT 4
#define TAG_NULL (uint64_t(0))
#define TAG_INT (uint64_t(1))
#define TAG_FLOAT (uint64_t(2))
#define TAG_MINI_STRING (uint64_t(4))
#define TAG_STRING (uint64_t(5))
#define TAG_ARRAY (uint64_t(6))
#define TAG_OBJECT (uint64_t(7))
#define TAG_BOOL (uint64_t(8))

#define TAG_MASK (uint64_t(0x0F))

#define TAG_VALUE_SHIFT(value, tag) (((value) << TAG_SHIFT) | (tag))
#define UNTAG_VALUE_SHIFT(value) ((value) >> TAG_SHIFT)
#define TAG_VALUE_MASK(value, tag) (((value) & ~TAG_MASK) | tag)
#define UNTAG_VALUE_MASK(value) ((value) & ~TAG_MASK)
#define TAG_PTR(value, tag) TAG_VALUE_SHIFT((int64_t)value, tag)
#define UNTAG_PTR(value) ((void *)UNTAG_VALUE_SHIFT(value))

class json_t {
  uint64_t m_value;

public:
  // can not plain copy. Use std::move or explicit dup()
  json_t(const json_t &) = delete;
  json_t &operator=(const json_t &) = delete;
  json_t(const json_t &&) = delete;
  json_t &operator=(const json_t &&) = delete;

  // move constructor
  json_t(json_t &&other) {
    m_value = other.m_value;
    other.m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
  };

  // move assignment
  json_t &operator=(json_t &&other) {
    this->~json_t();
    m_value = other.m_value;
    other.m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
    return *this;
  };
  ~json_t() {
    if ((m_value & TAG_MASK) == TAG_STRING) {
      delete (std::string *)UNTAG_PTR(m_value);
    } else if (is_array()) {
      delete (std::vector<json_t> *)UNTAG_PTR(m_value);
    } else if (is_object()) {
      delete (std::unordered_map<std::string, json_t> *)UNTAG_PTR(m_value);
    }
    m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
  }

  json_t(const char *value) {
    m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
    *this = std::move(from_string(value));
  }
  json_t(const std::string &value) {
    m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
    *this = std::move(from_string(value));
  }
  json_t(int64_t value) : m_value(TAG_VALUE_SHIFT(value, TAG_INT)) {}
  json_t(int value) : m_value(TAG_VALUE_SHIFT(int64_t(value), TAG_INT)) {}
  json_t(double value) {
    union {
      double d;
      int64_t i;
    } u;
    u.d = value;
    m_value = TAG_VALUE_MASK(u.i, TAG_FLOAT);
  }
  json_t(bool value) : m_value(TAG_VALUE_SHIFT(value, TAG_BOOL)) {}

  json_t(std::initializer_list<json_t> value) {
    bool is_object = false;
    for (auto &v : value) {
      if (v.is_array() && v.as_array().size() == 2 &&
          v.as_array()[0].is_string()) {
        is_object = true;
        break;
      }
    }
    if (is_object) {
      m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
      *this = std::move(from_object({}));
      for (auto &v : value) {
        auto &pair = v.as_array();
        as_object()[pair[0].as_string()] = pair[1].dup();
      }
    } else {
      m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
      *this = std::move(from_array(value));
    }
  }

  json_t() : m_value(TAG_VALUE_SHIFT(0, TAG_NULL)) {}
  json_t(typeof(nullptr)) : m_value(TAG_VALUE_SHIFT(0, TAG_NULL)) {}

  static json_t from_int(int64_t nvalue) {
    json_t v;
    v.m_value = TAG_VALUE_SHIFT(nvalue, TAG_INT);
    return v;
  }

  static json_t from_float(double nvalue) {
    // print the hex bytes of the double
    union {
      double d;
      int64_t i;
    } u;
    u.d = nvalue;
    json_t v;
    v.m_value = TAG_VALUE_MASK(u.i, TAG_FLOAT);
    return v;
  }

  static json_t from_bool(bool nvalue) {
    json_t v;
    v.m_value = TAG_VALUE_SHIFT(int64_t(nvalue), TAG_BOOL);
    return v;
  }

  static json_t from_null() {
    json_t v;
    v.m_value = TAG_VALUE_SHIFT(0, TAG_NULL);
    return v;
  }

  static json_t from_string(const std::string_view &nvalue) {
    int nvaluelength = nvalue.size();
    // less or equal than 7 chars, inside the value (the 8th is the tag, and set
    // to 0)
    if (nvaluelength < 7) {
      json_t v;
      memcpy((void *)&v.m_value, &nvalue[0], nvaluelength);
      v.m_value = (v.m_value << TAG_SHIFT) | TAG_MINI_STRING;
      return v;
    } else {
      // reserve memory and all that
      json_t v;
      std::string *mvalue = new std::string(nvalue);
      v.m_value = TAG_PTR((int64_t)mvalue, TAG_STRING);
      return v;
    }
  }

  static json_t from_array(std::initializer_list<json_t> nvalue) {
    json_t v;

    std::vector<json_t> *mvalue = new std::vector<json_t>(nvalue.size());

    int i = 0;
    for (auto &v : nvalue) {
      (*mvalue)[i] = v.dup();
      ++i;
    }

    v.m_value = TAG_PTR((int64_t)mvalue, TAG_ARRAY);
    return v;
  }
  static json_t from_array(std::vector<json_t> &&nvalue) {
    json_t v;

    std::vector<json_t> *mvalue = new std::vector<json_t>();
    *mvalue = std::move(nvalue);

    v.m_value = TAG_PTR((int64_t)mvalue, TAG_ARRAY);
    return v;
  }

  // same as from array, but use unordered_map
  static json_t
  from_object(std::initializer_list<std::pair<std::string, json_t>> nvalue) {
    json_t v;

    auto *mvalue = new std::unordered_map<std::string, json_t>();

    for (auto &v : nvalue) {
      mvalue->insert(std::make_pair(v.first, v.second.dup()));
    }

    v.m_value = TAG_PTR((int64_t)mvalue, TAG_OBJECT);
    return v;
  }

  json_t dup() const {
    if (is_array()) {
      auto value = json_t::from_array({});
      for (auto &v : as_array()) {
        value.as_array().push_back(v.dup());
      }
      return value;
    } else if (is_object()) {
      auto value = json_t::from_object({});
      for (auto &v : as_object()) {
        value.as_object().insert(std::make_pair(v.first, v.second.dup()));
      }
      return value;
    } else if (is_string()) {
      return json_t::from_string(as_string());
    }
    json_t v;
    v.m_value = m_value;
    return v;
  }

  bool is_int() const { return (m_value & TAG_MASK) == TAG_INT; }

  bool is_float() const { return (m_value & TAG_MASK) == TAG_FLOAT; }

  bool is_string() const {
    return ((m_value & TAG_MASK) == TAG_STRING) ||
           ((m_value & TAG_MASK) == TAG_MINI_STRING);
  }

  bool is_array() const { return (m_value & TAG_MASK) == TAG_ARRAY; }

  bool is_object() const { return (m_value & TAG_MASK) == TAG_OBJECT; }

  bool is_null() const { return (m_value & TAG_MASK) == TAG_NULL; }

  bool is_bool() const { return (m_value & TAG_MASK) == TAG_BOOL; }

  int64_t as_int() const {
    assert(is_int());
    int64_t value = UNTAG_VALUE_SHIFT(m_value);
    return value;
  }

  std::string as_string() const {
    assert(is_string());
    if ((m_value & TAG_MASK) == TAG_MINI_STRING) {
      char value[8] = {0};
      int64_t svalue = (m_value >> TAG_SHIFT);
      memcpy((void *)value, &svalue, 8);
      value[7] = '\0';
      return std::string(value);
    } else if ((m_value & TAG_MASK) == TAG_STRING) {
      std::string *value = (std::string *)UNTAG_PTR(m_value);
      return *value;
    } else
      return "unknown";
  }

  double as_float() const {
    assert(is_float());
    union {
      double d;
      int64_t i;
    } u;
    u.i = UNTAG_VALUE_MASK(m_value);
    return u.d;
  }

  bool as_bool() const {
    assert(is_bool());
    return UNTAG_VALUE_SHIFT(m_value);
  }

  std::vector<json_t> &as_array() const {
    assert(is_array());
    return *((std::vector<json_t> *)UNTAG_PTR(m_value));
  }

  std::unordered_map<std::string, json_t> &as_object() const {
    assert(is_object());
    return *((std::unordered_map<std::string, json_t> *)UNTAG_PTR(m_value));
  }

  void push_back(json_t &&value) { as_array().push_back(std::move(value)); }

  void insert(const std::string &key, json_t &&value) {
    as_object()[key] = std::move(value);
  }

  int64_t raw_value() const { return m_value; }

  std::string dump() const { return to_string(); }
  std::string dump(int indent) const { return to_string(); }
  std::string to_string() const;

  json_t &operator[](const std::string &key) const {
    assert(is_object());
    return as_object()[key];
  }
  json_t &operator[](const std::string &key) {
    assert(is_object());
    return as_object()[key];
  }
  json_t &operator[](int index) const {
    assert(is_array());
    return as_array()[index];
  }
  json_t &operator[](int index) {
    assert(is_array());
    return as_array()[index];
  }

  static json_t parse(const std::string &json);
};

#undef TAG_ARRAY
#undef TAG_BOOL
#undef TAG_FLOAT
#undef TAG_INT
#undef TAG_MASK
#undef TAG_MINI_STRING
#undef TAG_NULL
#undef TAG_OBJECT
#undef TAG_PTR
#undef TAG_SHIFT
#undef TAG_STRING
#undef TAG_VALUE
#undef TAG_VALUE_MASK
#undef TAG_VALUE_SHIFT
#undef UNTAG_VALUE_MASK
#undef UNTAG_VALUE_SHIFT
} // namespace rtpmididns
