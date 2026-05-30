/**
 * Phase 3: partial device query matching (stored-query bracket semantics).
 */
#pragma once

#include "device_identity.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns {

/**
 * Subset match against device identities. Non-bracketed fields constrain the
 * match; bracketed fields are retained for storage but ignored by matches().
 */
struct device_query_t {
  std::string type_prefix;
  std::vector<device_identity_field_t> fields;

  static std::optional<device_query_t> parse(std::string_view text);
  std::string serialize() const;

  bool matches(const device_identity_t &identity) const;
};

/** True when the identity string contains bracketed stored-query fields. */
bool device_identity_is_stored_query(const device_identity_t &identity);
bool device_identity_is_stored_query(std::string_view text);

/** Indices into @a devices of every identity satisfied by @a query. */
std::vector<size_t> find_all_matching(const device_query_t &query,
                                      const std::vector<device_identity_t> &devices);

} // namespace rtpmididns
