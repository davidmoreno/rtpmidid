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

/// Control-socket and listener messages (spec: control-socket-jsondm): the
/// requester reply/event set a connection actor accepts, and the network/
/// control listeners' control variants. Lives next to
/// `control_socket_actor.hpp` / `network_rtpmidi_listener_actor.hpp`.

#pragma once

#include "mailbox.hpp"
#include "message_core.hpp"
#include "mdns_messages.hpp"
#include "network_messages.hpp"
#include "peer_messages.hpp"
#include "router_messages.hpp"
#include "rtpmidi_server_messages.hpp"
#include "worker_messages.hpp"

namespace rtpmididns {

/// A control-socket connection actor: the requester reply/event set. The
/// `exports` status section is gathered from the ALSA listener and the
/// rtpmidi server (`exports_status_resp_t`; additive, lazy-rtpmidi-
/// connections).
using connection_control_t =
    std::variant<stop_t, ack_t, peer_ids_result_t, status_head_t,
                 peer_status_resp_t, peer_command_resp_t, mdns_status_resp_t,
                 dns_resolved_t, peer_event_t, exports_status_resp_t>;
/// The control listener: connection exit notices.
using control_listener_control_t = std::variant<stop_t, stopped_t>;
/// A control-socket connection actor's mailbox.
using connection_mailbox_t = mailbox_t<std::monostate, connection_control_t>;
/// The control listener's mailbox.
using control_listener_mailbox_t =
    mailbox_t<std::monostate, control_listener_control_t>;

} // namespace rtpmididns
