/**
 * Phase 5: directed connection restore planning (query fan-out).
 */
#pragma once

#include "connection_db.hpp"
#include "device_identity.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rtpmididns {

struct online_device_t {
  peer_id_t peer_id = 0;
  device_identity_t identity;
};

struct connect_action_t {
  peer_id_t from = 0;
  peer_id_t to = 0;
};

/** Match @a side (stored query or legacy stable id) against online devices. */
std::vector<size_t> match_side_to_devices(const std::string &side,
                                          const std::vector<online_device_t> &online);

/** Expand saved connections to directed router connect actions. */
std::vector<connect_action_t> plan_connection_restore(
    const std::vector<stored_connection_t> &saved,
    const std::vector<online_device_t> &online,
    const std::function<bool(peer_id_t, peer_id_t)> &has_edge);

class midirouter_t;

/** Collect online devices with their identities from a live router. */
std::vector<online_device_t>
collect_online_devices_from_router(const std::shared_ptr<midirouter_t> &router);

} // namespace rtpmididns
