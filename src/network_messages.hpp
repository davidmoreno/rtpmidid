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

} // namespace rtpmididns
