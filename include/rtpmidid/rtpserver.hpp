/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include "./rtppeer.hpp"
#include <map>
#include <memory>

struct sockaddr_in6;

namespace rtpmidid {
class rtpserver {
public:
  // Stupid RTPMIDI uses initiator_id sometimes and ssrc other times.

  // Maps the peers to the initiator_id.
  std::map<uint32_t, std::shared_ptr<rtppeer>> initiator_to_peer;
  // Maps the peers to the ssrc.
  std::map<uint32_t, std::shared_ptr<rtppeer>> ssrc_to_peer;

  // Callbacks to call when new connections
  signal_t<std::shared_ptr<rtppeer>> connected_event;
  signal_t<const io_bytes_reader &> midi_event;

  std::string name;

  int midi_socket;
  int control_socket;

  uint16_t midi_port;
  uint16_t control_port;

  rtpserver(std::string name, const std::string &port);
  ~rtpserver();

  // Returns the peer for that packet, or nullptr
  std::shared_ptr<rtppeer> get_peer_by_packet(io_bytes_reader &b,
                                              rtppeer::port_e port);
  void create_peer_from(io_bytes_reader &&buffer, struct sockaddr_in6 *cliaddr,
                        rtppeer::port_e port);

  void send_midi_to_all_peers(const io_bytes_reader &bufer);

  void data_ready(rtppeer::port_e port);
  void sendto(const io_bytes_reader &b, rtppeer::port_e port,
              struct sockaddr_in6 *, int remote_base_port);
};
} // namespace rtpmidid
