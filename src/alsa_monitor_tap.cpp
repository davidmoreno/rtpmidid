/**
 * Phase 7: ALSA aconnect monitor tap implementation.
 */
#include "alsa_monitor_tap.hpp"

#include "rtpmidid/logger.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

namespace {

struct monitor_tap_edge_t {
  peer_id_t from = 0;
  peer_id_t to = 0;
};

struct monitor_tap_session_t {
  std::vector<monitor_tap_edge_t> tap_edges;
};

std::mutex tap_mutex;
std::unordered_map<std::string, monitor_tap_session_t> tap_sessions;

bool router_has_edge(const midirouter_t &router, peer_id_t from, peer_id_t to) {
  if (from == 0 || to == 0)
    return false;
  const auto targets = router.send_targets_for(from);
  return std::find(targets.begin(), targets.end(), to) != targets.end();
}

} // namespace

void setup_alsa_monitor_taps(
    const std::shared_ptr<aseq_t> &aseq,
    const std::shared_ptr<midirouter_t> &router, uint8_t target_client,
    uint8_t target_port, peer_id_t target_peer, peer_id_t monitor_peer,
    const std::string &session_uuid,
    const std::function<peer_id_t(uint8_t client, uint8_t port)> &ensure_alsa_peer) {
  if (!aseq || !router || !ensure_alsa_peer || session_uuid.empty())
    return;

  const aseq_t::port_t target{target_client, target_port};
  monitor_tap_session_t session;

  for (const auto &sub : aseq->enumerate_subscriptions()) {
    if (sub.to_client != target_client || sub.to_port != target_port)
      continue;

    const peer_id_t source_peer =
        ensure_alsa_peer(static_cast<uint8_t>(sub.from_client),
                         static_cast<uint8_t>(sub.from_port));
    if (source_peer == 0)
      continue;

    /* Router path already tees via tee_monitor_edges; only tap pure aconnect. */
    if (router_has_edge(*router, source_peer, target_peer))
      continue;
    if (router_has_edge(*router, source_peer, monitor_peer))
      continue;

    router->connect_blocking(source_peer, monitor_peer);
    session.tap_edges.push_back(monitor_tap_edge_t{source_peer, monitor_peer});
    INFO("alsa_monitor_tap: session {} tap {} -> monitor {} (aconnect {}:{} -> "
         "{}:{})",
         session_uuid.substr(0, 8), source_peer, monitor_peer, sub.from_client,
         sub.from_port, target_client, target_port);
  }

  if (!session.tap_edges.empty()) {
    std::lock_guard<std::mutex> lock(tap_mutex);
    tap_sessions[session_uuid] = std::move(session);
  }
}

void teardown_alsa_monitor_taps(const std::shared_ptr<midirouter_t> &router,
                                const std::string &session_uuid) {
  if (!router || session_uuid.empty())
    return;

  monitor_tap_session_t session;
  {
    std::lock_guard<std::mutex> lock(tap_mutex);
    const auto it = tap_sessions.find(session_uuid);
    if (it == tap_sessions.end())
      return;
    session = std::move(it->second);
    tap_sessions.erase(it);
  }

  for (const auto &edge : session.tap_edges) {
    router->disconnect(edge.from, edge.to);
    INFO("alsa_monitor_tap: session {} removed tap {} -> {}", session_uuid.substr(0, 8),
         edge.from, edge.to);
  }
}

} // namespace rtpmididns
