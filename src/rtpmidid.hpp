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

#include <string>
#include <set>
#include "./aseq.hpp"
#include "./mdns.hpp"

namespace rtpmidid{
  struct config_t;
  class rtpserver;
  class rtpclient;
  class parse_buffer_t;

  struct client_info{
    std::string name;
    std::string address;
    uint16_t port;
    uint16_t use_count;
    // This might be not intialized if not really connected yet.
    std::shared_ptr<::rtpmidid::rtpclient> peer;
  };
  struct server_info{
    std::string name;
    std::string address;
    uint16_t port;
    // This might be not intialized if not really connected yet.
    std::shared_ptr<::rtpmidid::rtpserver> peer;
  };

  class rtpmidid {
  public:
    std::string name;
    ::rtpmidid::aseq seq;
    ::rtpmidid::mdns mdns;
    // Local port id to client_info for connections
    std::map<uint8_t, client_info> known_clients;
    std::map<uint8_t, server_info> known_servers;
    std::vector<std::shared_ptr<::rtpmidid::rtpserver>> servers;
    char export_port_next_id = 'A';
    char max_export_port_next_id = 'Z';
    std::set<std::string> known_mdns_peers;

    rtpmidid(config_t *config);

    // Manual connect to a server.
    std::optional<uint8_t> add_rtpmidi_client(const std::string &name, const std::string &address, uint16_t port);

    void recv_rtpmidi_event(int port, parse_buffer_t &midi_data);
    void recv_alsamidi_event(int port, snd_seq_event_t *ev);

    void setup_mdns();
    void setup_alsa_seq();

    uint16_t add_rtpmidid_server(const std::string &name, uint16_t port);

    /// New export port, with next id
    void add_export_port();
    // Random aseq_ to be created
    void add_export_port(char id);
    // Use a specific port
    void add_export_port(char id, uint8_t aseq_port);
    void remove_client(uint8_t alsa_port);
  };
}
