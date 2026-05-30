#include "peer_export.hpp"

namespace rtpmididns {

std::optional<std::string> peer_export_t::compute_stable_id() const {
  return compute_stable_id_impl();
}

} // namespace rtpmididns
