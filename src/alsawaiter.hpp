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

#pragma once
#include "aseq.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/rtpclient.hpp"
#include "rtpmidid/signal.hpp"

namespace rtpmididns {

/**
 * @short A local ALSA port waiting for connections. When connected connects to
 * a remote rtpmidi.
 *
 * The connection is empty, but if we connect to this port, it does the
 * rtppeer creation and connect to the remote server.
 *
 * This is used both by mDNS, that creates and removes this port, and for
 * manually adding remote rtpmidi ports.
 */
class alsawaiter_t : public midipeer_t {
public:
  std::string name;
  std::vector<rtpmidid::rtpclient_t::endpoint_t> endpoints;
  // Currently connected, if any
  std::string hostname;
  std::string port;

  // For each ALSA port connected, when arrives to 0, it disconnects
  int connection_count = 0;
  uint8_t alsaport;
  std::shared_ptr<aseq_t> aseq;
  connection_t<aseq_t::port_t, const std::string &> subscribe_connection;
  connection_t<aseq_t::port_t> unsubscribe_connection;
  connection_t<snd_seq_event_t *> alsamidi_connection;

  mididata_to_alsaevents_t mididata_decoder;
  mididata_to_alsaevents_t mididata_encoder;

  midipeer_id_t rtpmidiclientworker_peer_id;
  // std::shared_ptr<rtpmidid::rtpclient_t> rtpclient;
  connection_t<rtpmidid::rtppeer_t::disconnect_reason_e> disconnect_connection;

  alsawaiter_t(const std::string &name, const std::string &hostname,
               const std::string &port, std::shared_ptr<aseq_t> aseq);
  ~alsawaiter_t() override;

  void send_midi(midipeer_id_t from, const mididata_t &) override;
  json_t status() override;

  void add_endpoint(const std::string &hostname, const std::string &port);
  void connect_to_remote_server();
  void disconnect_from_remote_server();
};

} // namespace rtpmididns
