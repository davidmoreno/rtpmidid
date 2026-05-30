/**
 * Hostname helper retained for device_identity_from_peer.
 */
#pragma once

#include <string>

namespace rtpmididns {

bool stable_id_is_real_hostname(const std::string &hostname);

} // namespace rtpmididns
