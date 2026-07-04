/**
 * Phase 2: key=value device identity grammar.
 */
#include "device_identity.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace rtpmididns {

namespace {

constexpr std::array<char, 6> kEscapeChars = {'\\', ':', ',', '=', '[', ']'};

bool needs_escape(char c) {
  for (char e : kEscapeChars) {
    if (c == e)
      return true;
  }
  return false;
}

struct parse_cursor_t {
  std::string_view text;
  size_t pos = 0;

  bool at_end() const { return pos >= text.size(); }

  char peek() const { return text[pos]; }

  bool consume(char c) {
    if (at_end() || text[pos] != c)
      return false;
    ++pos;
    return true;
  }

  std::optional<std::string> read_unescaped_until(char stop,
                                                  bool allow_end = false) {
    std::string out;
    while (!at_end()) {
      const char c = text[pos];
      if (c == '\\') {
        if (pos + 1 >= text.size())
          return std::nullopt;
        out += text[pos + 1];
        pos += 2;
        continue;
      }
      if (c == stop)
        return out;
      out += c;
      ++pos;
    }
    if (allow_end || stop == '\0')
      return out;
    return std::nullopt;
  }
};

std::optional<std::vector<device_identity_field_t>>
parse_fields(std::string_view fields_text) {
  if (fields_text.empty())
    return std::nullopt;

  parse_cursor_t cur{fields_text};
  std::vector<device_identity_field_t> fields;
  std::unordered_set<std::string> seen_keys;

  while (!cur.at_end()) {
    device_identity_field_t field;
    field.bracketed = cur.consume('[');

    auto key = cur.read_unescaped_until('=');
    if (!key || key->empty() || !cur.consume('='))
      return std::nullopt;

    const char value_stop = field.bracketed ? ']' : ',';
    auto value = cur.read_unescaped_until(value_stop, !field.bracketed);
    if (!value || value->empty())
      return std::nullopt;

    if (field.bracketed && !cur.consume(']'))
      return std::nullopt;

    field.key = std::move(*key);
    field.value = std::move(*value);

    if (!seen_keys.insert(field.key).second)
      return std::nullopt;

    fields.push_back(std::move(field));

    if (cur.at_end())
      break;
    if (!cur.consume(','))
      return std::nullopt;
    if (cur.at_end())
      return std::nullopt;
  }

  return fields;
}

void sort_fields_canonical(std::vector<device_identity_field_t> &fields) {
  std::sort(fields.begin(), fields.end(),
            [](const device_identity_field_t &a,
               const device_identity_field_t &b) { return a.key < b.key; });
}

} // namespace

bool device_identity_field_t::operator==(const device_identity_field_t &o) const {
  return key == o.key && value == o.value && bracketed == o.bracketed;
}

std::string device_identity_t::escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (needs_escape(c)) {
      out += '\\';
      out.push_back(c);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string device_identity_t::unescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\') {
      if (i + 1 >= s.size())
        break;
      out.push_back(s[i + 1]);
      ++i;
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

std::optional<device_identity_t> device_identity_t::parse(std::string_view text) {
  const auto colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0)
    return std::nullopt;

  const std::string type_prefix(text.substr(0, colon));
  const auto fields = parse_fields(text.substr(colon + 1));
  if (!fields || fields->empty())
    return std::nullopt;

  device_identity_t id;
  id.type_prefix = type_prefix;
  id.fields = *fields;
  sort_fields_canonical(id.fields);
  return id;
}

std::string device_identity_t::serialize() const {
  std::string out = type_prefix;
  out += ':';

  auto sorted = fields;
  sort_fields_canonical(sorted);

  for (size_t i = 0; i < sorted.size(); ++i) {
    if (i > 0)
      out += ',';
    if (sorted[i].bracketed)
      out += '[';
    out += escape(sorted[i].key);
    out += '=';
    out += escape(sorted[i].value);
    if (sorted[i].bracketed)
      out += ']';
  }
  return out;
}

std::optional<std::string> device_identity_t::find(std::string_view key) const {
  for (const auto &f : fields) {
    if (f.key == key)
      return f.value;
  }
  return std::nullopt;
}

bool device_identity_t::operator==(const device_identity_t &o) const {
  return type_prefix == o.type_prefix && fields == o.fields;
}

namespace {

// Ephemeral fields that do NOT contribute to device identity.
// A device is the same physical endpoint regardless of these fields.
const std::unordered_map<std::string_view, std::vector<std::string_view>>
kEphemeralFields = {
    {"rtpmidi_client", {"port"}},
    {"rtpmidi_server", {"port"}},
    {"rtpmidi_session", {}},          // hostname+service already canonical
    {"rtpmidi_multi",   {"port"}},
    {"alsa_listener",   {"hostname", "port", "local_udp_port"}},
    {"alsa_seq",        {}},           // handled specially below
    {"alsa_multi",      {}},
    {"rawmidi",         {}},
};

bool is_strippable_field(const std::string &type_prefix,
                         const std::string &key,
                         bool has_name_field) {
  const auto it = kEphemeralFields.find(type_prefix);
  if (it == kEphemeralFields.end())
    return false;
  for (const auto &strip : it->second) {
    if (strip == key)
      return true;
  }
  // alsa_seq: strip client/port only when a name field identifies the device
  if (type_prefix == "alsa_seq" && has_name_field) {
    return key == "client" || key == "port";
  }
  return false;
}

} // namespace

std::string device_identity_t::canonical_key() const {
  const bool has_name = find("name").has_value();

  device_identity_t canonical;
  canonical.type_prefix = type_prefix;
  for (const auto &f : fields) {
    if (!is_strippable_field(type_prefix, f.key, has_name))
      canonical.fields.push_back(f);
  }
  return canonical.serialize();
}

std::string display_name_from_identity(
    const device_identity_t &id,
    const std::optional<std::string> &override_name) {
  if (override_name && !override_name->empty())
    return *override_name;
  for (const char *key : {"name", "service", "device"}) {
    if (const auto v = id.find(key))
      return *v;
  }
  if (auto c = id.find("client")) {
    if (auto p = id.find("port"))
      return *c + ":" + *p;
    return *c;
  }
  return id.serialize();
}

} // namespace rtpmididns
