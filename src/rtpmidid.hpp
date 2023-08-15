/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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

#include "./aseq.hpp"
#include "rtpmidid/rtppeer.hpp"
#include "rtpmidid/signal.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/poller.hpp>
#include <set>
#include <string>

namespace rtpmidid {
struct config_t;
class rtpserver;
class rtpclient;
class rtppeer;
class io_bytes_reader;
class io_bytes_writer;
struct address_t {
  std::string address;
  std::string port;
};

struct client_info_t {
  uint32_t client_id;
  std::string name;
  std::vector<address_t> addresses;
  int addr_idx; // Current try address, if any.
  uint16_t use_count;
  // This might be not intialized if not really connected yet.
  std::shared_ptr<::rtpmidid::rtpclient> peer;
  uint8_t aseq_port;
  uint connect_attempts = 0;

  connection_t<aseq::port_t, const std::string &> subscribe_connection;
  connection_t<aseq::port_t> unsubscribe_connection;
  connection_t<snd_seq_event_t *> midi_from_alsaseq;
  connection_t<const io_bytes_reader &> midi_from_network;
  connection_t<rtppeer::disconnect_reason_e> disconnect_event;
};

struct server_info_t {
  uint32_t server_id;
  std::string name;
  // This might be not intialized if not really connected yet.
  std::shared_ptr<::rtpmidid::rtppeer> peer;
  std::shared_ptr<::rtpmidid::rtpserver> server;
  std::vector<aseq::port_t> connected_to;
  uint8_t alsa_port;

  connection_t<std::shared_ptr<::rtpmidid::rtppeer>> connected_event;
  connection_t<const io_bytes_reader &> midi_from_network;
  connection_t<snd_seq_event_t *> midi_from_alsaseq;
  connection_t<rtppeer::disconnect_reason_e> disconnect_event;
  connection_t<aseq::port_t> seq_unsubscribe;
};

class rtpmidid_t {
public:
  std::string name;
  ::rtpmidid::aseq seq;
  ::rtpmidid::mdns_rtpmidi mdns_rtpmidi;
  // Local port id to client_info for connections
  std::vector<client_info_t> known_clients;
  std::vector<server_info_t> known_servers;
  uint32_t max_peer_id;

  std::vector<server_info_t>::iterator find_known_server(uint32_t id) {
    return std::find_if(
        known_servers.begin(), known_servers.end(),
        [id](server_info_t &serverinfo) { return id == serverinfo.server_id; });
  }
  std::vector<client_info_t>::iterator find_known_client(uint32_t id) {
    return std::find_if(
        known_clients.begin(), known_clients.end(),
        [id](client_info_t &serverinfo) { return id == serverinfo.client_id; });
  }
  std::vector<client_info_t>::iterator
  find_known_client_by_alsa_port(uint8_t port) {
    return std::find_if(known_clients.begin(), known_clients.end(),
                        [port](client_info_t &serverinfo) {
                          return port == serverinfo.aseq_port;
                        });
  }

  // std::vector<std::shared_ptr<::rtpmidid::rtpserver>> servers;
  // std::map<aseq::port_t, std::shared_ptr<::rtpmidid::rtpserver>>
  // alsa_to_server;
  std::set<std::string> known_mdns_peers;

  connection_t<aseq::port_t, const std::string &> alsaport_subscribe_connection;
  connection_t<const std::string &, const std::string &, const std::string &>
      mdns_discover_connection;
  connection_t<const std::string &> mdns_remove_connection;

  rtpmidid_t(const config_t &config);

  // Manual connect to a server.
  std::optional<uint8_t> add_rtpmidi_client(const std::string &hostdescription);
  std::optional<uint8_t> add_rtpmidi_client(const std::string &name,
                                            const std::string &address,
                                            const std::string &port);
  void remove_rtpmidi_client(const std::string &name);

  void recv_rtpmidi_event(int port, io_bytes_reader &midi_data);
  void recv_alsamidi_event(int port, snd_seq_event_t *ev);

  void alsamidi_to_midiprotocol(snd_seq_event_t *ev, io_bytes_writer &buffer);

  void setup_alsa_seq();
  void setup_mdns();
  void announce_rtpmidid_server(const std::string &name, uint16_t port);
  void unannounce_rtpmidid_server(const std::string &name, uint16_t port);
  void connect_client(const std::string &name, int aseqport);
  void disconnect_client(int aseqport,
                         //  disconnect_reason_e ellidded
                         int reason);
  // An import server is one that for each discovered connection, creates
  // the alsa ports
  std::shared_ptr<rtpserver>
  add_rtpmidid_import_server(const std::string &name, const std::string &port);

  // An export server is one that exports a local ALSA seq port. It is announced
  // with the aseq port name and so on. There is one per connection to the
  // "Network"
  std::shared_ptr<rtpserver> add_rtpmidid_export_server(const std::string &name,
                                                        uint8_t alsaport,
                                                        aseq::port_t &from);

  void remove_client(uint8_t alsa_port);
};
} // namespace rtpmidid
