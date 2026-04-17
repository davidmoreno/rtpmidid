/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 *
 * Shared JSON-RPC dispatch for Unix control socket and web UI.
 */
#pragma once

#include "json.hpp"
#include <memory>
#include <string>

namespace rtpmidid {
class mdns_rtpmidi_t;
}

namespace rtpmididns {
class midirouter_t;
class aseq_t;

struct control_rpc_context_t {
  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<aseq_t> aseq;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;
};

json_t mdns_status(const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns);

/** JSON-RPC: @p req must contain `method`, `params`, `id`. Returns `{id, result}` or `{id, error}`. */
json_t control_rpc_dispatch(control_rpc_context_t &ctx, const json_t &req);

} // namespace rtpmididns
