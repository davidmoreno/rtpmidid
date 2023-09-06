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

#include "rtpmidilistener.hpp"
#include "factory.hpp"
#include "json.hpp"
#include "midirouter.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"

namespace rtpmididns {

extern std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

rtpmidilistener_t::rtpmidilistener_t(const std::string &name,
                                     const std::string &port,
                                     std::shared_ptr<aseq_t> aseq_)
    : aseq(aseq_), server(name, port) {
  if (mdns)
    mdns->announce_rtpmidi(name, server.control_port);

  connected_connection = server.connected_event.connect(
      [this](std::shared_ptr<rtpmidid::rtppeer_t> peer) {
        DEBUG("Got connection from {}", peer->remote_name);
        auto alsa_id = router->add_peer(make_alsapeer(peer->remote_name, aseq));
        auto peer_id = router->add_peer(make_rtppeer(peer));
        router->connect(alsa_id, peer_id);
        router->connect(peer_id, alsa_id);
      });
}

void rtpmidilistener_t::send_midi(midipeer_id_t from, const mididata_t &) {}

json_t rtpmidilistener_t::status() {
  std::vector<json_t> peers;
  for (auto &peer : server.peers) {
    auto &peerpeer = peer.peer;
    peers.push_back({
        //
        {"latency_ms", peerpeer->latency / 10.0},
        {"status", peerpeer->status},
        {"sequence_number", peerpeer->seq_nr},
        {"sequence_number_ack", peerpeer->seq_nr_ack},
        {"local",
         {
             {"name", peerpeer->local_name}, {"ssrc", peerpeer->local_ssrc}, //
         }},                                                                 //
        {
            "remote",
            {
                {"sequence_number", peerpeer->remote_seq_nr},
                {"name", peerpeer->remote_name},
                {"ssrc", peerpeer->remote_ssrc},
                {"port", peer.port},
                {"address", peer.address} //
            }                             //
        }
        //
    });
  }

  return json_t{
      {"type", "rtpmidi_listener"}, //
      {"peers", peers}              //
  };
}

} // namespace rtpmididns
