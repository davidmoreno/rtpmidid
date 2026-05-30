#include "peer_device.hpp"

namespace rtpmididns {

std::optional<std::string> peer_device_t::compute_stable_id() const {
  return compute_stable_id_impl();
}

} // namespace rtpmididns
