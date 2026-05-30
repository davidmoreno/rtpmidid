/**
 * Export peer base.
 */
#pragma once

#include "midipeer.hpp"
#include "peer_kind.hpp"

namespace rtpmididns {

/** Exposes a local MIDI endpoint to the outside (network or another transport). */
class peer_export_t : public midipeer_t {
public:
  const char *get_type() const override final {
    return peer_kind_wire_type(peer_kind());
  }

protected:
  virtual peer_kind_e peer_kind() const = 0;
};

} // namespace rtpmididns
