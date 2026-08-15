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

/// ALSA listener messages (lazy-rtpmidi-connections, design D1/D2): hosted
/// and waiting port management. Lives next to `alsa_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "rtpmidi_server_messages.hpp" // alsa_port_session_t, set_registered, ...
#include <string>

namespace rtpmididns {

/// Ask the ALSA listener to create a seq port. With `waiting` the port is
/// created unregistered (a waiting port for a remote; registered on the
/// first ALSA subscription). The reply (`alsa_port_result_t`) carries the
/// seq port number and the router id when registered.
struct alsa_create_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  std::string meta;
  bool waiting = false;  // waiting port: created unregistered
  std::string remote;    // session identity of the remote (waiting ports)
};
/// Ask the ALSA listener to remove a port, by router id or (waiting ports
/// have none) by seq port number.
struct alsa_remove_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  peer_id_t peer_id = 0;
  uint8_t seq_port = 0;
};
/// Ask the ALSA listener to create a per-connection seq port subscribed
/// both ways to an existing local port (auto-exported seq devices). The
/// port is created registered; reply is `alsa_port_result_t`.
struct alsa_subscribe_port_t {
  hdr_t hdr;
  mailbox_handle_t reply_to;
  std::string name;
  std::string target; // local "client:port"
};

/// The ALSA listener's accepted control messages: port commands, the
/// server's session-state updates and registration requests, and the
/// exports status gather (its register acks land on small internal reply
/// mailboxes, not here).
using alsa_control_t = std::variant<stop_t, alsa_create_port_t,
                                    alsa_remove_port_t, alsa_subscribe_port_t,
                                    alsa_port_session_t,
                                    alsa_port_set_registered_t,
                                    exports_status_req_t>;
/// The ALSA actor's mailbox (has a data lane: it receives midi_to_wire).
using alsa_mailbox_t = mailbox_t<data_message_t, alsa_control_t>;

} // namespace rtpmididns
