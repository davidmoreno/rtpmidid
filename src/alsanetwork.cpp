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

#include "alsanetwork.hpp"
#include "alsapeer.hpp"
#include "aseq.hpp"
#include "factory.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidiserver.hpp"
#include <memory>
#include <utility>

using namespace rtpmididns;

alsanetwork_t::alsanetwork_t(const std::string &name, midirouter_t *router_)
    : seq(name), router(router_) {
  port = seq.create_port("Network");
  subscribe_connection = seq.subscribe_event[port].connect(
      [this](rtpmidid::aseq::port_t port, const std::string &name) {
        new_alsa_connection(port, name);
      });
  // TODO unsubscribe and midi data
};

std::pair<midipeer_id_t, midipeer_id_t>
alsanetwork_t::new_alsa_connection(const rtpmidid::aseq::port_t &port,
                                   const std::string &name) {
  std::shared_ptr<midipeer_t> alsapeer = make_alsapeer(name, seq);
  std::shared_ptr<midipeer_t> networkpeer = make_rtpmidiserver(name);
  auto alsapeer_id = router->add_peer(alsapeer);
  auto networkpeer_id = router->add_peer(networkpeer);

  router->connect(alsapeer_id, networkpeer_id);
  router->connect(networkpeer_id, alsapeer_id);

  aseqpeers[port] = alsapeer_id;

  return std::make_pair(alsapeer_id, networkpeer_id);
}

void alsanetwork_t::alsaseq_event(snd_seq_event_t *event) {
  auto peerI = aseqpeers.find(
      rtpmidid::aseq::port_t{event->source.client, event->source.port});
  if (peerI == aseqpeers.end()) {
    WARNING("Unknown source for event {}:{}!", event->source.client,
            event->source.port);
    for (auto &it : aseqpeers) {
      DEBUG("Known: {}:{}", it.first.client, it.first.port);
    }
    return;
  }

  uint8_t buffer[1024];
  rtpmidid::io_bytes_writer writer(buffer, sizeof(buffer));
  alsatrans.write(event, writer);
  auto midi = mididata_t(writer);

  router->send_midi(peerI->second, midi);
}
