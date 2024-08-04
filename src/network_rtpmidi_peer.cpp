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

#include "network_rtpmidi_peer.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/rtppeer.hpp"
#include "utils.hpp"
#include <memory>

namespace rtpmididns {
network_rtpmidi_peer_t::network_rtpmidi_peer_t(
    std::shared_ptr<rtpmidid::rtppeer_t> peer_)
    : peer(peer_) {

  midi_connection =
      peer->midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        router->send_midi(peer_id, mididata_t{data});
      });

  status_change_event_connection = peer->status_change_event.connect(
      [this](rtpmidid::rtppeer_t::status_e reason) {
        if (reason < rtpmidid::rtppeer_t::status_e::DISCONNECTED) {
          return;
        }
        DEBUG("Peer disconnected: {}. Remove rtpmidi peer and alsa port too.",
              reason);
        rtpmidid::poller.call_later([this] {
          router->peer_connection_loop(peer_id, [this](auto other_peer) {
            router->remove_peer(other_peer->peer_id);
          });
          router->remove_peer(peer_id);
        });
      });
}

network_rtpmidi_peer_t::~network_rtpmidi_peer_t() {}

void network_rtpmidi_peer_t::send_midi(midipeer_id_t from,
                                       const mididata_t &data) {
  // DEBUG("Send midi: {}", data.size());
  peer->send_midi(data);
};

json_t network_rtpmidi_peer_t::status() {
  return json_t{
      {"name", peer->remote_name},      //
      {"type", "network:rtpmidi:peer"}, //
      {"peer", peer_status(*peer)},
  };
};

} // namespace rtpmididns
