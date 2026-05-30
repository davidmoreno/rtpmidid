/**
 * Phase 3: device query partial matching.
 */
#include "device_query.hpp"

#include <algorithm>

namespace rtpmididns {

std::optional<device_query_t> device_query_t::parse(std::string_view text) {
  const auto identity = device_identity_t::parse(text);
  if (!identity)
    return std::nullopt;
  return device_query_t{identity->type_prefix, identity->fields};
}

std::string device_query_t::serialize() const {
  return device_identity_t{type_prefix, fields}.serialize();
}

bool device_query_t::matches(const device_identity_t &identity) const {
  if (type_prefix != identity.type_prefix)
    return false;

  for (const auto &qf : fields) {
    if (qf.bracketed)
      continue;

    const auto value = identity.find(qf.key);
    if (!value || *value != qf.value)
      return false;
  }
  return true;
}

bool device_identity_is_stored_query(const device_identity_t &identity) {
  for (const auto &f : identity.fields) {
    if (f.bracketed)
      return true;
  }
  return false;
}

bool device_identity_is_stored_query(std::string_view text) {
  const auto identity = device_identity_t::parse(text);
  if (!identity)
    return false;
  return device_identity_is_stored_query(*identity);
}

std::vector<size_t> find_all_matching(const device_query_t &query,
                                      const std::vector<device_identity_t> &devices) {
  std::vector<size_t> out;
  for (size_t i = 0; i < devices.size(); ++i) {
    if (query.matches(devices[i]))
      out.push_back(i);
  }
  return out;
}

} // namespace rtpmididns
