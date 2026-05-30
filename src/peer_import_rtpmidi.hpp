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
#include "peer_import.hpp"
#include "dm_json_status.hpp"
#include "rtpmidid/rtpserver.hpp"
#include <memory>
#include <string>
#include <unordered_set>

namespace rtpmididns {
class aseq_t;
class midirouter_t;

/**
 * @short A rtpmidi server that creates local listen ALSA ports
 *
 * This midipeer does not actually send or receive data, but creates
 * a local ALSA peer and a rtpmidi peer and connects them.
 */
/** RTP-MIDI server that spawns ALSA + session device peers per inbound client. */
class peer_import_rtpmidi_t : public peer_import_t {
public:
  std::shared_ptr<aseq_t> aseq;
  rtpmidid::rtpserver_t server;
  rtpmidid::rtpserver_t::midi_event_t::connection_t midi_connection;
  rtpmidid::rtpserver_t::status_change_event_t::connection_t
      status_change_connection;
  /* Identity-tracks rtppeers we've already wrapped into a midipeer pair, so
     duplicate CONNECTED events (e.g. from broken remotes that resend IN to an
     already-connected peer) don't double-wrap and leak ALSA ports. The raw
     pointer is fine as the key because we erase on DISCONNECTED before the
     shared_ptr drops; if we miss a cleanup the worst case is a stale entry
     that gets recycled by a later rtppeer at the same address. */
  std::unordered_set<rtpmidid::rtppeer_t *> wrapped_peers_;

  peer_import_rtpmidi_t(const std::string &name,
                                   const std::string &port,
                                   std::shared_ptr<aseq_t> aseq);
  void send_midi(midipeer_id_t from, const mididata_t &) override;
  router_peer_row_t status() const override;


protected:
  peer_kind_e peer_kind() const override {
    return peer_kind_e::import_rtpmidi;
  }
};
} // namespace rtpmididns
