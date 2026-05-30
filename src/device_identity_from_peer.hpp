/**
 * Phase 4: map router peer status rows to key=value device identities.
 */
#pragma once

#include "device_identity.hpp"
#include "dm_json_status.hpp"

#include <optional>

namespace rtpmididns {

std::optional<device_identity_t>
compute_device_identity(const router_peer_row_t &row);

} // namespace rtpmididns
