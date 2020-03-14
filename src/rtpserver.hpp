/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#pragma once

#include <map>
#include <memory>
#include "./rtppeer.hpp"

struct sockaddr_in6;

namespace rtpmidid{
  class rtpserver{
  public:
    // Stupid RTPMIDI uses initiator_id sometimes and ssrc other times.

    // Maps the peers to the initiator_id.
    std::map<uint32_t, std::shared_ptr<rtppeer>> initiator_to_peer;
    // Maps the peers to the ssrc.
    std::map<uint32_t, std::shared_ptr<rtppeer>> ssrc_to_peer;

    // Callbacks to call when new connections
    signal_t<std::shared_ptr<rtppeer>> connected_event;
    signal_t<parse_buffer_t &> midi_event;

    std::string name;

    int midi_socket;
    int control_socket;

    uint16_t midi_port;
    uint16_t control_port;

    rtpserver(std::string name, const std::string &port);
    ~rtpserver();

    // Returns the peer for that packet, or nullptr
    std::shared_ptr<rtppeer> get_peer_by_packet(parse_buffer_t &b, rtppeer::port_e port);
    std::shared_ptr<rtppeer> get_peer_by_ssrc(uint32_t ssrc);
    void create_peer_from(parse_buffer_t &buffer, struct sockaddr_in6 *cliaddr, rtppeer::port_e port);

    void send_midi_to_all_peers(parse_buffer_t &bufer);

    void data_ready(rtppeer::port_e port);
    void sendto(const parse_buffer_t &b, rtppeer::port_e port, struct sockaddr_in6 *, int remote_base_port);
  };
}
