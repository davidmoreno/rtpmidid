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

/// Actor message protocol (spec: actor-message-protocol; design D6/D7).
///
/// This header is the assembly point only: it includes the message types
/// from their homes next to the actors (message_core.hpp for the universal
/// lifecycle/reply types, router_messages.hpp, peer_messages.hpp,
/// mdns_messages.hpp, alsa_messages.hpp, worker_messages.hpp,
/// network_messages.hpp, control_messages.hpp, and data_message.hpp for the
/// data plane) and defines the transport envelope `control_message_t` —
/// the inter-actor wire format. Each actor's mailbox control lane holds a
/// narrow per-actor variant instead, so an actor never sees a message
/// class it does not accept.

#pragma once

#include "alsa_messages.hpp"
#include "control_messages.hpp"
#include "data_message.hpp"
#include "mdns_messages.hpp"
#include "message_core.hpp"
#include "network_messages.hpp"
#include "peer_messages.hpp"
#include "router_messages.hpp"
#include "worker_messages.hpp"
#include <variant>

namespace rtpmididns {

/// The transport control envelope: the union of every control message in
/// the system. It is the inter-actor wire format only — each actor's
/// mailbox control lane holds a narrow per-actor variant and
/// converts/validates on receipt, so an actor never sees a message class
/// it does not accept. Never pays for inline MIDI storage (design D6).
using control_message_t =
    std::variant<std::monostate, stop_t, stopped_t, actor_died_t, reap_actor_t,
                 peer_event_t, control_payload_t, register_peer_t,
                 unregister_peer_t, spawn_peer_t, registered_t, remove_peer_t,
                 connect_t, disconnect_t, status_req_t, status_head_t,
                 peer_status_req_t, peer_status_resp_t, peer_command_t,
                 peer_command_resp_t, subscribe_events_t, unsubscribe_events_t,
                 stop_all_t, ack_t, peer_ids_result_t, worker_job_t,
                 dns_resolved_t, udp_datagram_t, udp_peer_gone_t,
                 mdns_status_req_t, mdns_status_resp_t, mdns_announce_t,
                 mdns_unannounce_t, mdns_remove_t, alsa_create_port_t,
                 alsa_remove_port_t>;

} // namespace rtpmididns
