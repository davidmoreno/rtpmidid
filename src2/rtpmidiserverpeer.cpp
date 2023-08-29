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

#include "rtpmidiserverpeer.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"

namespace rtpmididns {
extern std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

rtpmidiserverpeer_t::rtpmidiserverpeer_t(const std::string &name)
    : name_(name), server(name, "") {
  if (mdns)
    mdns->announce_rtpmidi(name, server.control_port);

  midi_connection =
      server.midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        // DEBUG("Got data: {}", data.size());
        if (!router) {
          WARNING("Bad configured peer");
          return;
        }
        // rtpmididns::mididata_t mididata(data.start, data.pos());
        router->send_midi(this->peer_id, data);
      });
}
rtpmidiserverpeer_t::~rtpmidiserverpeer_t() {
  if (mdns)
    mdns->unannounce_rtpmidi(name_, server.control_port);
}

void rtpmidiserverpeer_t::send_midi(midipeer_id_t from,
                                    const mididata_t &mididata) {
  packets_recv++;
  server.send_midi_to_all_peers(mididata);
}

json_t rtpmidiserverpeer_t::status() {
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
      {"name", name_},                 //
      {"type", "rtpmidiserverpeer_t"}, //
      {
          "stats", //
          {{"recv", packets_recv}, {"sent", packets_sent}}
          //
      },
      {"port", server.midi_port},
      {"peers", //
       peers}
      //
  };
}
} // namespace rtpmididns
