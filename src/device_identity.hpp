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

  /**
   * Returns a new identity with fields that do NOT define device identity
   * stripped (e.g. port numbers). The canonical form deduplicates the same
   * physical endpoint across port changes, daemon restarts, etc.
   *
   * Rules are type-prefix-specific and easy to adjust per-type.
   */
  std::string canonical_key() const;
};

/** Human-readable display name from an identity, with optional override. */
std::string display_name_from_identity(const device_identity_t &id,
                                       const std::optional<std::string> &override_name = std::nullopt);

} // namespace rtpmididns
