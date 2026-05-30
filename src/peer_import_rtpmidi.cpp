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

#include "peer_import_rtpmidi.hpp"
#include "factory.hpp"
#include "midirouter.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "utils.hpp"

namespace rtpmididns {

extern std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

peer_import_rtpmidi_t::peer_import_rtpmidi_t(
    const std::string &name, const std::string &port,
    std::shared_ptr<aseq_t> aseq_)
    : aseq(aseq_), server(name, port) {
  if (mdns)
    mdns->announce_rtpmidi(name, server.port());

  status_change_connection = server.status_change_event.connect(
      [this](std::shared_ptr<rtpmidid::rtppeer_t> peer,
             rtpmidid::rtppeer_t::status_e status) {
        if (rtpmidid::rtppeer_t::is_disconnected(status)) {
          /* The wrapper midipeers (peer_device_rtpmidi_session_t) clean themselves
             up on DISCONNECTED via their own status_change hook; we just
             forget the tracking entry so future reconnects at the same
             rtppeer address get re-wrapped. */
          wrapped_peers_.erase(peer.get());
          return;
        }
        if (status != rtpmidid::rtppeer_t::status_e::CONNECTED) {
          return;
        }
        /* Defensive de-dup: even if upstream (lib/rtppeer.cpp) re-fires
           CONNECTED for the same rtppeer (split-brain accept of duplicate
           IN), we only wrap it into router midipeers once. Without this,
           every duplicate IN from a misbehaving remote would create a fresh
           local_alsa_peer + network_rtpmidi_peer pair, leak an ALSA port,
           and feed connection_db with a chain of stale stable-id matches. */
        if (!wrapped_peers_.insert(peer.get()).second) {
          DEBUG("Duplicate CONNECTED event for already-wrapped rtppeer {}; "
                "ignoring (split-brain accept by remote {}).",
                static_cast<void *>(peer.get()), peer->remote_name);
          return;
        }
        DEBUG("Got connection from {}", peer->remote_name);
        auto alsa_id =
            router->add_peer(make_peer_device_alsa_seq(peer->remote_name, aseq));
        auto session_id = router->add_peer(make_peer_device_rtpmidi_session(peer));
        router->connect(alsa_id, session_id);
        router->connect(session_id, alsa_id);
      });
}

void peer_import_rtpmidi_t::send_midi(midipeer_id_t from,
                                                 const mididata_t &) {}

router_peer_row_t peer_import_rtpmidi_t::status() const {
  router_peer_row_t row;
  std::vector<rtp_peer_status_t> plist;
  for (const auto &peer : server.peers) {
    plist.push_back(rtp_peer_status_from(*peer.peer));
  }
  row.peers = std::move(plist);
  row.name = server.name;
  listening_ports_t lp;
  lp.name = server.name;
  lp.control_port = server.port();
  lp.midi_port = static_cast<uint16_t>(server.port() + 1);
  row.listening = lp;
  return row;
}



} // namespace rtpmididns
