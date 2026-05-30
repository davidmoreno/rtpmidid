/**
 * Hostname helper retained for device_identity_from_peer.
 */
#include "peer_stable_id.hpp"

namespace rtpmididns {

bool stable_id_is_real_hostname(const std::string &hostname) {
  return !hostname.empty() && hostname != "null";
}

} // namespace rtpmididns
