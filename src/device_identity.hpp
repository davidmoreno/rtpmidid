/**
 * Phase 2: key=value device identity grammar (parse, serialize, escaping).
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns {

/** One key=value field; bracketed fields are stored-query metadata (Phase 3). */
struct device_identity_field_t {
  std::string key;
  std::string value;
  bool bracketed = false;

  bool operator==(const device_identity_field_t &o) const;
};

/**
 * Full or partial device identity: `type_prefix:key=value,...`.
 * Serialize always emits fields in canonical (lexicographic key) order.
 */
struct device_identity_t {
  std::string type_prefix;
  std::vector<device_identity_field_t> fields;

  static std::optional<device_identity_t> parse(std::string_view text);
  std::string serialize() const;

  static std::string escape(std::string_view s);
  static std::string unescape(std::string_view s);

  /** First field with @a key, or nullopt if absent. */
  std::optional<std::string> find(std::string_view key) const;

  bool operator==(const device_identity_t &o) const;
};

} // namespace rtpmididns
