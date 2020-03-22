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
#include <memory>
#include "./aseq.hpp"
#include "./poller.hpp"
#include "./mdns_rtpmidi.hpp"

namespace rtpmidid{
  struct config_t;
  class rtpserver;
  class rtpclient;
  class rtppeer;
  struct parse_buffer_t;
  struct address_t{
    std::string address;
    std::string port;
  };

  struct client_info{
    std::string name;
    std::vector<address_t> addresses;
    int addr_idx; // Current try address, if any.
    uint16_t use_count;
    // This might be not intialized if not really connected yet.
    std::shared_ptr<::rtpmidid::rtpclient> peer;
    uint8_t aseq_port;
    uint connect_attempts = 0;
  };
  struct server_conn_info{
    std::string name;
    // This might be not intialized if not really connected yet.
    std::shared_ptr<::rtpmidid::rtppeer> peer;
    std::shared_ptr<::rtpmidid::rtpserver> server;
  };

  class rtpmidid_t {
  public:
    std::string name;
    ::rtpmidid::aseq seq;
    ::rtpmidid::mdns_rtpmidi mdns_rtpmidi;
    // Local port id to client_info for connections
    std::map<uint8_t, client_info> known_clients;
    std::map<uint8_t, server_conn_info> known_servers_connections;
    std::vector<std::shared_ptr<::rtpmidid::rtpserver>> servers;
    std::map<aseq::port_t, std::shared_ptr<::rtpmidid::rtpserver>> alsa_to_server;
    std::set<std::string> known_mdns_peers;

    rtpmidid_t(config_t *config);

    // Manual connect to a server.
    std::optional<uint8_t> add_rtpmidi_client(const std::string &hostdescription);
    std::optional<uint8_t> add_rtpmidi_client(const std::string &name, const std::string &address, const std::string &port);
    void remove_rtpmidi_client(const std::string &name);

    void recv_rtpmidi_event(int port, parse_buffer_t &midi_data);
    void recv_alsamidi_event(int port, snd_seq_event_t *ev);

    void alsamidi_to_midiprotocol(snd_seq_event_t *ev, parse_buffer_t &buffer);

    void setup_alsa_seq();
    void setup_mdns();
    void announce_rtpmidid_server(const std::string &name, uint16_t port);
    void unannounce_rtpmidid_server(const std::string &name, uint16_t port);
    void connect_client(int aseqport);

    // An import server is one that for each discovered connection, creates the alsa ports
    std::shared_ptr<rtpserver> add_rtpmidid_import_server(const std::string &name, const std::string &port);


    // An export server is one that exports a local ALSA seq port. It is announced with the
    // aseq port name and so on. There is one per connection to the "Network"
    std::shared_ptr<rtpserver> add_rtpmidid_export_server(const std::string &name, uint8_t alsaport, aseq::port_t &from);

    void remove_client(uint8_t alsa_port);
  };
}
