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
#include "peer_export.hpp"
#include "dm_json_status.hpp"
#include "rtpmidid/rtpserver.hpp"
#include "rtpmidid/signal.hpp"
#include "rtpmidid/utils.hpp"
#include <string>

namespace rtpmididns {
/**
 * @short Creates a new rtpmidi server, all connections share the data bus
 *
 * The idea is that ALSA connected a port to Network, so we export the rtpmidi
 * connection.
 *
 * This is this connection. As several clients can connect, any data goes to
 * the ALSA side, and any data from ALSA goes to all the clients.
 */
/** RTP-MIDI server that fans out to all connected clients (shared bus). */
class peer_export_rtpmidi_server_t : public peer_export_t {
  NON_COPYABLE_NOR_MOVABLE(peer_export_rtpmidi_server_t);

public:
  std::string name_;
  rtpmidid::rtpserver_t server;
  int use_count = 0;

  rtpmidid::rtpserver_t::midi_event_t::connection_t midi_connection;
  rtpmidid::rtpserver_t::status_change_event_t::connection_t
      status_change_connection;

  peer_export_rtpmidi_server_t(const std::string &name,
                             const std::string &udp_port);
  ~peer_export_rtpmidi_server_t() override;
  void send_midi(midipeer_id_t from, const mididata_t &) override;
  router_peer_row_t status() const override;

  static std::optional<std::string>
  stable_id_from_row(const router_peer_row_t &row);

protected:
  peer_kind_e peer_kind() const override {
    return peer_kind_e::export_rtpmidi_server;
  }
  std::optional<std::string> compute_stable_id_impl() const override;
};
} // namespace rtpmididns
