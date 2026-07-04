/**
 * Phase 5: connection restore planning.
 */
#include "connection_restore.hpp"

#include "device_query.hpp"

#include <unordered_set>

namespace rtpmididns {

std::vector<size_t> match_side_to_devices(
    const std::string &side, const std::vector<online_device_t> &online) {
  std::vector<size_t> matches;

  if (const auto query = device_query_t::parse(side)) {
    for (size_t i = 0; i < online.size(); ++i) {
      if (query->matches(online[i].identity))
        append_unique(matches, i);
    }
    if (!matches.empty())
      return matches;
  }

  for (size_t i = 0; i < online.size(); ++i) {
    if (online[i].identity.serialize() == side)
      append_unique(matches, i);
  }
  return matches;
}

std::vector<connect_action_t> plan_connection_restore(
    const std::vector<stored_connection_t> &saved,
    const std::vector<online_device_t> &online,
    const std::function<bool(peer_id_t, peer_id_t)> &has_edge) {
  std::vector<connect_action_t> actions;
  if (online.empty())
    return actions;

  std::unordered_set<uint64_t> seen;
  const auto action_key = [](peer_id_t from, peer_id_t to) {
    return (static_cast<uint64_t>(from) << 32) |
           static_cast<uint64_t>(to);
  };

  const auto maybe_add = [&](peer_id_t from, peer_id_t to) {
    if (from == to || from == 0 || to == 0)
      return;
    if (has_edge && has_edge(from, to))
      return;
    const auto key = action_key(from, to);
    if (!seen.insert(key).second)
      return;
    actions.push_back(connect_action_t{from, to});
  };

  for (const auto &conn : saved) {
    if (!conn.enabled)
      continue;

    const auto match_a = match_side_to_devices(conn.side_a, online);
    const auto match_b = match_side_to_devices(conn.side_b, online);
    if (match_a.empty() || match_b.empty())
      continue;

    for (const size_t ai : match_a) {
      for (const size_t bi : match_b) {
        const peer_id_t peer_a = online[ai].peer_id;
        const peer_id_t peer_b = online[bi].peer_id;
        if (conn.direction == connection_direction_e::a2b ||
            conn.direction == connection_direction_e::both)
          maybe_add(peer_a, peer_b);
        if (conn.direction == connection_direction_e::b2a ||
            conn.direction == connection_direction_e::both)
          maybe_add(peer_b, peer_a);
      }
    }
  }

  return actions;
}

} // namespace rtpmididns
