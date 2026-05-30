/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 */
#pragma once

#include "midipeer.hpp"
#include "peer_kind.hpp"

namespace rtpmididns {

/** Brings an outside MIDI endpoint into the local router graph. */
class peer_import_t : public midipeer_t {
public:
  const char *get_type() const override final {
    return peer_kind_wire_type(peer_kind());
  }

  std::optional<std::string> compute_stable_id() const override;

protected:
  virtual peer_kind_e peer_kind() const = 0;
  virtual std::optional<std::string>
  compute_stable_id_impl() const = 0;
};

} // namespace rtpmididns
