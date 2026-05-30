/**
 * Phase 3: device query partial matching.
 */
#include "device_query.hpp"

#include <algorithm>

namespace rtpmididns {

namespace {

const device_identity_field_t *
find_field(const device_identity_t &identity, const std::string &key) {
  const auto it = std::find_if(
      identity.fields.begin(), identity.fields.end(),
      [&](const device_identity_field_t &f) { return f.key == key; });
  if (it == identity.fields.end())
    return nullptr;
  return &*it;
}

} // namespace

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

    const auto *match = find_field(identity, qf.key);
    if (!match || match->value != qf.value)
      return false;
  }
  return true;
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
