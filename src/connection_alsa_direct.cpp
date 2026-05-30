/**
 * Phase 7: pure ALSA direct connection planning.
 */
#include "connection_alsa_direct.hpp"

#include "device_query.hpp"
#include "peer_stable_id.hpp"

#include <unordered_set>
#include <utility>

namespace rtpmididns {

namespace {

std::optional<std::pair<std::string, std::string>>
parse_alsa_stable_id(const std::string &stable_id) {
  static const std::string kPrefix = "alsa:";
  if (stable_id.size() <= kPrefix.size())
    return std::nullopt;
  if (stable_id.compare(0, kPrefix.size(), kPrefix) != 0)
    return std::nullopt;
  const std::string rest = stable_id.substr(kPrefix.size());
  const auto pos = rest.find(':');
  if (pos == std::string::npos || pos == 0 || pos + 1 == rest.size())
    return std::nullopt;
  auto unescape = [](std::string s) {
    for (char &c : s) {
      if (c == '|')
        c = ':';
    }
    return s;
  };
  return std::make_pair(unescape(rest.substr(0, pos)),
                        unescape(rest.substr(pos + 1)));
}

std::optional<std::pair<std::string, std::string>>
parse_alsa_seq_identity(const std::string &identity_key) {
  const auto id = device_identity_t::parse(identity_key);
  if (!id || id->type_prefix != "alsa_seq")
    return std::nullopt;
  std::optional<std::string> client;
  std::optional<std::string> port;
  for (const auto &f : id->fields) {
    if (f.key == "client")
      client = f.value;
    else if (f.key == "port")
      port = f.value;
  }
  if (!client || !port)
    return std::nullopt;
  return std::make_pair(*client, *port);
}

std::optional<std::pair<std::string, std::string>>
alsa_side_names(const std::string &side) {
  if (auto legacy = parse_alsa_stable_id(side))
    return legacy;
  return parse_alsa_seq_identity(side);
}

void append_unique(std::vector<size_t> &out, size_t index) {
  if (std::find(out.begin(), out.end(), index) == out.end())
    out.push_back(index);
}

aseq_t::port_t port_from_row(const alsa_seq_port_row_t &row) {
  return aseq_t::port_t{static_cast<uint8_t>(row.client),
                        static_cast<uint8_t>(row.port)};
}

uint64_t link_key(const aseq_t::port_t &from, const aseq_t::port_t &to) {
  return (static_cast<uint64_t>(from.client) << 24) |
         (static_cast<uint64_t>(from.port) << 16) |
         (static_cast<uint64_t>(to.client) << 8) |
         static_cast<uint64_t>(to.port);
}

} // namespace

bool is_direct_alsa_side(const std::string &side) {
  if (alsa_side_names(side).has_value())
    return true;
  const auto query = device_query_t::parse(side);
  return query.has_value() && query->type_prefix == "alsa_seq";
}

device_identity_t device_identity_from_alsa_port(const alsa_seq_port_row_t &port) {
  device_identity_t id;
  id.type_prefix = "alsa_seq";
  id.fields.push_back(device_identity_field_t{"client", port.client_name, false});
  id.fields.push_back(device_identity_field_t{"port", port.port_name, false});
  return id;
}

std::vector<size_t> match_alsa_ports_for_side(
    const std::string &side, const std::vector<alsa_seq_port_row_t> &ports) {
  std::vector<size_t> matches;
  std::vector<device_identity_t> identities;
  identities.reserve(ports.size());
  for (const auto &p : ports)
    identities.push_back(device_identity_from_alsa_port(p));

  if (const auto query = device_query_t::parse(side)) {
    for (size_t i = 0; i < identities.size(); ++i) {
      if (query->matches(identities[i]))
        append_unique(matches, i);
    }
    if (!matches.empty())
      return matches;
  }

  if (const auto exact = device_identity_t::parse(side)) {
    for (size_t i = 0; i < identities.size(); ++i) {
      if (identities[i] == *exact)
        append_unique(matches, i);
    }
    if (!matches.empty())
      return matches;
  }

  if (const auto names = alsa_side_names(side)) {
    for (size_t i = 0; i < ports.size(); ++i) {
      if (ports[i].client_name == names->first &&
          ports[i].port_name == names->second)
        append_unique(matches, i);
    }
  }
  return matches;
}

std::vector<alsa_aconnect_action_t>
plan_alsa_aconnect_actions(const std::vector<stored_connection_t> &saved,
                           const std::vector<alsa_seq_port_row_t> &ports) {
  std::vector<alsa_aconnect_action_t> actions;
  std::unordered_set<uint64_t> seen;

  const auto maybe_add = [&](const aseq_t::port_t &from, const aseq_t::port_t &to) {
    if (from == to)
      return;
    const auto key = link_key(from, to);
    if (!seen.insert(key).second)
      return;
    actions.push_back(alsa_aconnect_action_t{from, to});
  };

  for (const auto &pair : saved) {
    if (!pair.enabled)
      continue;
    if (!is_direct_alsa_side(pair.side_a) || !is_direct_alsa_side(pair.side_b))
      continue;

    const auto match_a = match_alsa_ports_for_side(pair.side_a, ports);
    const auto match_b = match_alsa_ports_for_side(pair.side_b, ports);
    if (match_a.empty() || match_b.empty())
      continue;

    for (const size_t ai : match_a) {
      for (const size_t bi : match_b) {
        const auto port_a = port_from_row(ports[ai]);
        const auto port_b = port_from_row(ports[bi]);
        if (pair.direction == connection_direction_e::a2b ||
            pair.direction == connection_direction_e::both)
          maybe_add(port_a, port_b);
        if (pair.direction == connection_direction_e::b2a ||
            pair.direction == connection_direction_e::both)
          maybe_add(port_b, port_a);
      }
    }
  }

  return actions;
}

} // namespace rtpmididns
