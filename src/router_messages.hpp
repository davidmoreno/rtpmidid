/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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
/// Router control-plane messages (spec: midi-routing, design D7): the
/// topology/peer-lifecycle commands the router accepts. Lives next to
/// `router_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "peer_messages.hpp" // peer_command_t (relayed peer commands)
#include "peer_status.hpp"
#include <functional>
#include <string>
#include <vector>

namespace rtpmididns {

/// Register a hosted peer id (e.g. an ALSA port) mapped to an existing
/// mailbox; the router assigns the id and acks it to the caller.
struct register_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  mailbox_handle_t mailbox;
  std::string type; // for status ("alsa", ...)
  std::string meta; // extra info (name, port, ...)
};
/// Remove a hosted peer id without touching its hosting actor.
struct unregister_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};
/// Spawn a standalone peer: the caller does all fallible preparation and
/// posts this prepared bundle; the router constructs the actor, spawns its
/// thread, registers its id, posts `registered{ids}` to the peer and acks
/// the caller (design D7 spawn flow).
struct spawn_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  /// Prepared move-only bundle: the router assigns the peer id first and
  /// calls the factory with it, so the actor is born knowing its id (its
  /// `stopped`/`actor_died` carry it back to the router). Returns the
  /// actor through the type-erased base: peers of any family (network,
  /// rawmidi, ...) share the spawn path.
  std::move_only_function<std::shared_ptr<actor_base_t>(const mailbox_handle_t &,
                                                       peer_id_t)>
      factory;
  std::string type;
  std::string meta;
};
/// Remove: immediate topology cut, `stop`, await `stopped` under deadline,
/// join, ack (design D7 stop/remove choreography).
struct remove_peer_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
};
struct connect_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t from = 0;
  peer_id_t to = 0;
};
struct disconnect_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t from = 0;
  peer_id_t to = 0;
};
/// Requester-driven status gather (design D7): the router answers with a
/// status head carrying router-assigned members and scatters
/// `peer_status_req{reply_to}` to each peer; peers answer the requester
/// directly.
struct peer_meta_t {
  peer_id_t id = 0;
  std::string type;
  std::vector<peer_id_t> send_to;
  peer_stats_t stats{0, 0};
};
struct status_req_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct status_head_t {
  hdr_t hdr;
  std::vector<peer_meta_t> peers;
  uint64_t router_data_drops = 0;
  uint64_t router_control_drops = 0;
};
/// Topology event subscription (design D7): the router pushes `peer_event`
/// to the subscriber's mailbox for each topology change.
struct subscribe_events_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
struct unsubscribe_events_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};
/// Bounded stop-all of spawned peers and ack (design D7 shutdown).
struct stop_all_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
};

/// The router's accepted control messages.
using router_control_t =
    std::variant<stop_t, stopped_t, actor_died_t, register_peer_t,
                 unregister_peer_t, spawn_peer_t, remove_peer_t, connect_t,
                 disconnect_t, status_req_t, peer_command_t,
                 subscribe_events_t, unsubscribe_events_t, stop_all_t>;
/// The router's mailbox.
using router_mailbox_t = mailbox_t<data_message_t, router_control_t>;

} // namespace rtpmididns
