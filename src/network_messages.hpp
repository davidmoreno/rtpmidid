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

/// Network listener <-> peer routing messages (design D2; tasks 5.2, 5.3):
/// raw UDP datagrams re-routed between the listener and its peers. Lives
/// next to `network_rtpmidi_listener_actor.hpp`.

#pragma once

#include "message_core.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rtpmididns {

/// RTP-MIDI port selector carried in routed datagrams.
enum class udp_port_e : uint8_t { control, midi };

/// Raw UDP datagram routed between network actors: a peer actor re-forwards
/// datagrams the SO_REUSEPORT hash misdelivered to it, and the listener
/// routes datagrams that landed on its accept sockets to the owning peer.
struct udp_datagram_t {
  udp_port_e port = udp_port_e::control;
  std::vector<uint8_t> data;
  std::string remote_ip; // numeric source address
  uint16_t remote_port = 0; // source port (control base)
};
/// Peer -> listener: the connection ended; drop the routing entries.
struct udp_peer_gone_t {
  uint32_t initiator_id = 0;
  uint32_t ssrc = 0;
};

// One-line rendering for mailbox drop diagnostics (see message_core.hpp).
inline std::string to_string(udp_port_e port) {
  switch (port) {
  case udp_port_e::control:
    return "control";
  case udp_port_e::midi:
    return "midi";
  }
  return "?";
}
inline std::string to_string(const udp_datagram_t &m) {
  return "udp_datagram{port=" + to_string(m.port) +
         ", len=" + std::to_string(m.data.size()) + ", remote=" +
         m.remote_ip + ":" + std::to_string(m.remote_port) + "}";
}

} // namespace rtpmididns
