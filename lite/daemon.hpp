/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "alsa_seq.hpp"
#include "config.hpp"

#include <memory>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtpserver.hpp>
#include <rtpmidid/utils.hpp>
#include <vector>

namespace rtpmidid_lite {

/**
 * @short The lite daemon.
 *
 * A hub between one ALSA port and any number of RTP-MIDI connections: it serves
 * incoming connections on `config.port` and optionally connects out to the
 * peers given with --connect. MIDI from ALSA goes to every connection, and MIDI
 * from every connection goes to ALSA.
 */
class daemon_t {
  NON_COPYABLE_NOR_MOVABLE(daemon_t)
public:
  explicit daemon_t(config_t config);
  ~daemon_t();

  /// Create the ALSA port, the server, the clients and announce with mDNS.
  void start();

private:
  struct client_t {
    std::unique_ptr<rtpmidid::rtpclient_t> client;
    rtpmidid::rtppeer_t::midi_event_t::connection_t midi_connection;
    rtpmidid::rtppeer_t::status_change_event_t::connection_t status_connection;
  };

  void start_alsa();
  void start_server();
  void start_clients();
  void start_mdns();
  void try_announce();

  config_t config_;
  std::unique_ptr<alsa_seq_t> alsa_;
  std::unique_ptr<rtpmidid::mdns_rtpmidi_t> mdns_;
  std::unique_ptr<rtpmidid::rtpserver_t> server_;
  std::vector<std::unique_ptr<client_t>> clients_;

  // Connections last, so they disconnect before the objects they refer to.
  rtpmidid::signal_t<const rtpmidid::io_bytes_reader &>::connection_t
      alsa_midi_connection_;
  rtpmidid::rtppeer_t::midi_event_t::connection_t server_midi_connection_;
  rtpmidid::rtpserver_t::status_change_event_t::connection_t
      server_status_connection_;

  rtpmidid::poller_t::timer_t announce_timer_;
  int announce_attempts_ = 0;
};

} // namespace rtpmidid_lite
