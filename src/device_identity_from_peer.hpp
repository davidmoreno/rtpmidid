/**
 * Phase 4: map router peer status rows to key=value device identities.
 */
#pragma once

#include "device_identity.hpp"
#include "dm_json_status.hpp"

#include <optional>
#include <string>

namespace rtpmidid {
class rtppeer_t;
}

namespace rtpmididns {

std::optional<device_identity_t>
compute_device_identity(const router_peer_row_t &row);

std::optional<device_identity_t>
identity_from_alsa_names(const std::string &client_name,
                         const std::string &port_name);

std::optional<device_identity_t>
identity_from_raw_device(const std::string &device, const std::string &name = {});

std::optional<device_identity_t>
identity_from_rtpmidi_remote(const std::string &hostname,
                             const std::string &service,
                             const std::string &port = {});

std::optional<device_identity_t>
identity_from_rtppeer(const rtpmidid::rtppeer_t &peer);

std::optional<device_identity_t>
identity_from_rtpclient_connect(const std::string &hostname,
                                const std::string &port,
                                const std::string &service);

} // namespace rtpmididns
