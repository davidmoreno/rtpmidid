/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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

#include <string>
#include "./aseq.hpp"
#include "./mdns.hpp"
#include "./rtppeer.hpp"

namespace rtpmidid{
  struct peer_info{
    std::string name;
    std::string address;
    uint16_t port;
    uint16_t use_count;
    // This might be not intialized if not really connected yet.
    std::shared_ptr<::rtpmidid::rtppeer> peer;
  };

  class rtpmidid {
  public:
    std::string name;
    ::rtpmidid::aseq seq;
    ::rtpmidid::mdns mdns;
    // Local port id to peer_info for connections
    std::map<uint8_t, peer_info> known_peers;

    rtpmidid(std::string &&name);

    // Manual connect to a server.
    void add_rtpmidi_client(const std::string &name, const std::string &address, uint16_t port);

    void recv_rtpmidi_event(int port, parse_buffer_t &midi_data);
    void recv_alsamidi_event(int port, snd_seq_event_t *ev);

    void setup_mdns();
    void setup_alsa_seq();

    void add_rtpmidid_server(const std::string &name);

    // Random aseq_ to be created
    void add_remote_peer(const std::string &id);
    // Use a specific port
    void add_remote_peer(const std::string &id, uint8_t aseq_port);
    void remove_peer(uint8_t alsa_port);
  };
}
