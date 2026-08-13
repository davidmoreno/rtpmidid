/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/// Peer-facing messages (spec: midipeer-typed-interface): the registered
/// gate, status/command requests and their replies. Lives next to
/// `peer_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "network_messages.hpp"
#include "peer_status.hpp"
#include "worker_messages.hpp"
#include <string>
#include <vector>

namespace rtpmididns {

/// Router -> spawned peer: gate for wire traffic (design D7: a spawned
/// peer does not process wire traffic until it receives this).
struct registered_t {
  std::vector<peer_id_t> ids;
};
struct peer_status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t target = 0;
};
struct peer_status_resp_t {
  hdr_t hdr;
  peer_id_t peer_id = 0;
  peer_status_variant_t status;
};
/// Peer command relay (design D7): the router relays the request only; the
/// peer replies the typed result directly to the requester's `reply_to`.
struct peer_command_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
  std::string cmd;
  std::string params_json;
};
struct peer_command_resp_t {
  hdr_t hdr;
  peer_id_t peer_id = 0;
  std::string result_json;
  bool is_error = false;
};

/// Spawned/hosted peers' accepted control messages: the registered gate,
/// status/command requests, topology events, routed datagrams (network
/// peers) and DNS results.
using peer_control_t =
    std::variant<stop_t, registered_t, peer_status_req_t, peer_command_t,
                 peer_event_t, dns_resolved_t, udp_datagram_t>;
/// A (spawned or hosted) peer's mailbox.
using peer_mailbox_t = mailbox_t<data_message_t, peer_control_t>;

} // namespace rtpmididns
