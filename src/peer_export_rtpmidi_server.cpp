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

#include "peer_export_rtpmidi_server.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "utils.hpp"

namespace rtpmididns {
extern std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

/**
 * @short A rtpmidi server that just sends data to another peer
 */
peer_export_rtpmidi_server_t::peer_export_rtpmidi_server_t(
    const std::string &name, const std::string &udp_port)
    : name_(name), server(name, udp_port) {
  if (mdns)
    mdns->announce_rtpmidi(name, server.port());

  midi_connection =
      server.midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        // DEBUG("Got data: {}", data.size());
        if (!router) {
          WARNING("Bad configured peer");
          return;
        }
        // rtpmididns::mididata_t mididata(data.start, data.pos());
        enqueue_to_router(mididata_t{data});
      });
  status_change_connection = server.status_change_event.connect(
      [this](std::shared_ptr<rtpmidid::rtppeer_t> peer,
             rtpmidid::rtppeer_t::status_e status) {
        if (status == rtpmidid::rtppeer_t::status_e::CONNECTED) {
          router->event(peer_id, midipeer_event_e::CONNECTED_PEER);
        } else if (rtpmidid::rtppeer_t::is_disconnected(status)) {
          router->event(peer_id, midipeer_event_e::DISCONNECTED_PEER);
        }
      });
}
peer_export_rtpmidi_server_t::~peer_export_rtpmidi_server_t() {
  if (mdns)
    mdns->unannounce_rtpmidi(name_, server.port());
}

void peer_export_rtpmidi_server_t::send_midi(midipeer_id_t from,
                                           const mididata_t &mididata) {
  server.send_midi_to_all_peers(mididata);
}

router_peer_row_t peer_export_rtpmidi_server_t::status() const {
  router_peer_row_t row;
  std::vector<rtp_peer_status_t> plist;
  for (const auto &peer : server.peers) {
    plist.push_back(rtp_peer_status_from(*peer.peer));
  }
  row.name = name_;
  row.port = static_cast<int32_t>(server.port());
  row.peers = std::move(plist);
  return row;
}

} // namespace rtpmididns
