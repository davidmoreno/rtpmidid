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

#include "json.hpp"
#include <string_view>

namespace rtpmididns {

enum class token_type_t {
  string = 1,
  number_int,
  number_double,
  boolean,
  null,
  lbracket = 10,
  rbracket,
  lbrace = 20,
  rbrace,
  comma = 30,
  colon,
  eof = 50,
  error
};

struct token_t {
  std::string value;
  token_type_t type;
};

class tokenizer_t {
public:
  std::string m_json;
  size_t m_pos = 0;
  int m_line = 1;
  int m_col_start_pos = 0;
  bool m_error = false;

  tokenizer_t(const std::string &json) : m_json(json) {}
  token_t next_token();
  void rewind_token(const token_t &token) {
    if (token.type == token_type_t::string) {
      m_pos -= 2; // skip the quotes
    }
    m_pos -= token.value.size();
  }
  bool read_token(const std::string &token);
};

bool tokenizer_t::read_token(const std::string &token) {
  if (m_pos + token.size() > m_json.size()) {
    return false;
  }
  if (m_json.substr(m_pos, token.size()) == token) {
    return true;
  }
  return false;
}

token_t tokenizer_t::next_token() {
  token_t ret;
  if (m_pos >= m_json.size()) {
    ret.type = token_type_t::eof;
    return ret;
  }
  char c = m_json[m_pos];
  while (isspace(c)) {
    if (c == '\n') {
      m_line++;
      m_col_start_pos = m_pos;
    }

    m_pos++;
    c = m_json[m_pos];
    if (m_pos >= m_json.size()) {
      ret.type = token_type_t::eof;
      return ret;
    }
  }

  switch (c) {
  case '[':
    ret.type = token_type_t::lbracket;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case ']':
    ret.type = token_type_t::rbracket;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case '{':
    ret.type = token_type_t::lbrace;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case '}':
    ret.type = token_type_t::rbrace;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case ',':
    ret.type = token_type_t::comma;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case ':':
    ret.type = token_type_t::colon;
    ret.value = m_json.substr(m_pos, 1);
    m_pos++;
    break;
  case 't':
    if (read_token("true")) {
      ret.type = token_type_t::boolean;
      ret.value = m_json.substr(m_pos, 4);
      m_pos += 4;
    } else {
      ret.type = token_type_t::error;
      ret.value = m_json.substr(m_pos, 4);
    }
    break;
  case 'f':
    if (read_token("false")) {
      ret.type = token_type_t::boolean;
      ret.value = m_json.substr(m_pos, 5);
      m_pos += 5;
    } else {
      ret.type = token_type_t::error;
      ret.value = m_json.substr(m_pos, 5);
    }
    break;
  case 'n':
    if (read_token("null")) {
      ret.type = token_type_t::null;
      ret.value = m_json.substr(m_pos, 4);
      m_pos += 4;
    } else {
      ret.type = token_type_t::error;
      ret.value = m_json.substr(m_pos, 4);
    }
    break;
  case '"': {
    ret.type = token_type_t::string;
    m_pos++;
    int start = m_pos;
    while (m_json[m_pos] != '"') {
      m_pos++;
    }
    ret.value = m_json.substr(start, m_pos - start);
    m_pos++;
  } break;
  default:
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t start = m_pos;
      ret.type = token_type_t::number_int;
      while (m_json[m_pos] >= '0' && m_json[m_pos] <= '9') {
        m_pos++;
      }
      if (m_json[m_pos] == '.') {
        ret.type = token_type_t::number_double;
        m_pos++;
        while (m_json[m_pos] >= '0' && m_json[m_pos] <= '9') {
          m_pos++;
        }
      }
      ret.value = m_json.substr(start, m_pos - start);
    }
  }
  return ret;
}

json_t parse_tokenizer(tokenizer_t &tokenizer) {
  json_t ret;
  token_t token = tokenizer.next_token();
  switch (token.type) {
  case token_type_t::string:
    ret = json_t::from_string(token.value);
    break;
  case token_type_t::number_int:
    ret =
        json_t::from_int(int64_t(std::stol(std::string(token.value), nullptr)));
    break;
  case token_type_t::number_double:
    ret = json_t::from_float(std::stod(std::string(token.value), nullptr));
    break;
  case token_type_t::boolean:
    ret = json_t::from_bool(token.value == "true");
    break;
  case token_type_t::null:
    ret = json_t::from_null();
    break;
  case token_type_t::lbracket:
    ret = json_t::from_array({});
    token = tokenizer.next_token();
    if (token.type == token_type_t::rbracket) {
      break;
    }
    tokenizer.rewind_token(token);
    while (true) {
      ret.as_array().push_back(parse_tokenizer(tokenizer));

      token = tokenizer.next_token();
      if (token.type == token_type_t::rbracket) {
        break;
      }
      if (token.type != token_type_t::comma) {
        tokenizer.m_error = true;
        return json_t();
      }
    }
    break;
  case token_type_t::lbrace:
    ret = json_t::from_object({});
    token = tokenizer.next_token();
    if (token.type == token_type_t::rbrace) {
      break;
    }
    while (true) {
      if (token.type != token_type_t::string) {
        tokenizer.m_error = true;
        return json_t();
      }
      std::string key = std::string(token.value);
      token = tokenizer.next_token();
      if (token.type != token_type_t::colon) {
        tokenizer.m_error = true;
        return json_t();
      }
      ret.as_object()[key] = parse_tokenizer(tokenizer);

      token = tokenizer.next_token();
      if (token.type == token_type_t::rbrace) {
        break;
      }
      if (token.type != token_type_t::comma) {
        tokenizer.m_error = true;
        return json_t();
      }
      token = tokenizer.next_token();
    }
    break;
  default:
    tokenizer.m_error = true;
    return json_t();
    break;
  }
  return ret;
}
json_t json_t::parse(const std::string &json) {
  json_t ret;
  tokenizer_t tokenizer(json);
  ret = parse_tokenizer(tokenizer);
  if (tokenizer.m_error) {
    fprintf(stderr, "Error parsing JSON at line %d:%ld\n", tokenizer.m_line,
            tokenizer.m_pos - tokenizer.m_col_start_pos);
    return json_t::from_null();
  }
  return ret;
}

std::string json_t::to_string() const {
  if (is_int()) {
    return std::to_string(as_int());
  } else if (is_float()) {
    return std::to_string(as_float());
  } else if (is_bool()) {
    return as_bool() ? "true" : "false";
  } else if (is_null()) {
    return "null";
  } else if (is_string()) {
    std::string s = as_string();
    return "\"" + s + "\"";
  } else if (is_array()) {
    if (as_array().size() == 0)
      return "[]";
    std::string s = "[";
    for (auto &v : as_array()) {
      s += v.to_string() + ", ";
    }
    // replace last comma with ]
    s[s.size() - 2] = ']';
    return s;
  } else if (is_object()) {
    if (as_object().size() == 0)
      return "{}";

    std::string s = "{";
    for (auto &v : as_object()) {
      s += "\"" + v.first + "\": " + v.second.to_string() + ", ";
    }
    s[s.size() - 2] = '}';
    s[s.size() - 1] = ' ';
    return s;
  } else {
    return "unknown";
  }
}
} // namespace rtpmididns