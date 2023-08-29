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
#include "json.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidiserverpeer.hpp"
#include <alsa/seqmid.h>
#include <memory>
#include <utility>

namespace rtpmididns {

alsanetwork_t::alsanetwork_t(const std::string &name,
                             std::shared_ptr<rtpmidid::aseq> aseq_)
    : seq(aseq_) {

  port = seq->create_port("Network");
  subscribe_connection = seq->subscribe_event[port].connect(
      [this](rtpmidid::aseq::port_t port, const std::string &name) {
        new_alsa_connection(port, name);
      });

  midi_connection = seq->midi_event[port].connect(
      [this](snd_seq_event_t *ev) { alsaseq_event(ev); });

  unsubscibe_connection = seq->unsubscribe_event[port].connect(
      [this](rtpmidid::aseq::port_t port) { remove_alsa_connection(port); });
  // TODO unsubscribe
};
alsanetwork_t::~alsanetwork_t() { seq->remove_port(port); }

midipeer_id_t
alsanetwork_t::new_alsa_connection(const rtpmidid::aseq::port_t &port,
                                   const std::string &name) {
  DEBUG("Create network peer {}", name);
  // std::shared_ptr<midipeer_t> alsapeer = make_alsapeer(name, seq);
  std::shared_ptr<midipeer_t> networkpeer = make_rtpmidiserver(name);
  // auto alsapeer_id = router->add_peer(alsapeer);
  auto networkpeer_id = router->add_peer(networkpeer);

  // router->connect(alsapeer_id, networkpeer_id);
  // router->connect(networkpeer_id, alsapeer_id);

  aseqpeers[port] = networkpeer_id;
  router->connect(networkpeer_id, peer_id);

  // return std::make_pair(alsapeer_id, networkpeer_id);
  return networkpeer_id;
}

void alsanetwork_t::remove_alsa_connection(const rtpmidid::aseq::port_t &port) {
  auto networkpeerI = aseqpeers.find(port);
  if (networkpeerI != aseqpeers.end()) {
    DEBUG("Removed ALSA port {}:{}, removing midipeer {}", port.client,
          port.port, networkpeerI->second);
    router->remove_peer(networkpeerI->second);
    aseqpeers.erase(port);
  } else {
    DEBUG("Removed ALSA port {}:{}, removing midipeer. NOT FOUND!", port.client,
          port.port);
    for (auto &peers : aseqpeers) {
      DEBUG("Known peer {}:{}", peers.first.port, peers.first.client);
    }
  }
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
  alsatrans_decoder.decode(event, writer);
  auto midi = mididata_t(writer);

  router->send_midi(peer_id, peerI->second, midi);
}

void alsanetwork_t::send_midi(midipeer_id_t from, const mididata_t &data) {
  for (auto &peer : aseqpeers) {
    // DEBUG("Look for dest alsa peer: {} == {} ? {}", peer.second, from,
    //       peer.second == from);
    if (peer.second == from) {
      auto mididata_copy =
          mididata_t(data); // Its just the pointers, not the data itself
      auto port = peer.first;
      alsatrans_encoder.encode(
          mididata_copy, [this, port](snd_seq_event_t *ev) {
            // DEBUG("Send to ALSA port {}:{}", port.client, port.port);
            snd_seq_ev_set_source(ev, this->port);
            snd_seq_ev_set_dest(ev, port.client, port.port);
            snd_seq_ev_set_direct(ev);
            snd_seq_event_output_direct(seq->seq, ev);
          });
    }
  }
}
json_t alsanetwork_t::status() {
  json_t connections{};
  for (auto &peer : aseqpeers) {
    auto port = peer.first;
    auto to = peer.second;
    connections.push_back({
        //
        {"alsa", fmt::format("{}:{}", port.client, port.port)},
        {"local", to} //
    });
  }

  return json_t{
      {"type", "alsanetwork_t"}, //
      {"name", seq->name},       //
      {"connections", connections}
      //
  };
}

} // namespace rtpmididns
