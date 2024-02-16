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

#include "local_alsa_multi_listener.hpp"
#include "aseq.hpp"
#include "factory.hpp"
#include "json.hpp"
#include "local_alsa_peer.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "network_rtpmidi_listener.hpp"
#include "network_rtpmidi_peer.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include <alsa/seqmid.h>
#include <memory>
#include <utility>

namespace rtpmididns {

local_alsa_multi_listener_t::local_alsa_multi_listener_t(
    const std::string &name_, std::shared_ptr<aseq_t> aseq_)
    : seq(aseq_), name(name_) {

  port = seq->create_port(name);
  subscribe_connection = seq->subscribe_event[port].connect(
      [this](aseq_t::port_t port, const std::string &name) {
        new_alsa_connection(port, name);
      });

  midi_connection = seq->midi_event[port].connect(
      [this](snd_seq_event_t *ev) { alsaseq_event(ev); });

  unsubscribe_connection = seq->unsubscribe_event[port].connect(
      [this](aseq_t::port_t port) { remove_alsa_connection(port); });
  // TODO unsubscribe
};
local_alsa_multi_listener_t::~local_alsa_multi_listener_t() {
  seq->remove_port(port);
}

midipeer_id_t
local_alsa_multi_listener_t::new_alsa_connection(const aseq_t::port_t &port,
                                                 const std::string &name) {
  DEBUG("New connection to network peer {}, from a local connection to {}",
        name, this->name);

  midipeer_id_t networkpeer_id = MIDIPEER_ID_INVALID;
  router->for_each_peer<network_rtpmidi_listener_t>(
      [&name, &networkpeer_id](auto *peer) {
        if (peer->name_ == name) {
          peer->use_count++;
          networkpeer_id = peer->peer_id;
          DEBUG("One more user for peer: {}, count: ", peer->peer_id,
                peer->use_count);
        }
      });

  if (networkpeer_id == MIDIPEER_ID_INVALID) {
    std::shared_ptr<midipeer_t> networkpeer =
        make_network_rtpmidi_listener(name);
    networkpeer_id = router->add_peer(networkpeer);

    aseqpeers[port] = networkpeer_id;
    router->connect(networkpeer_id, peer_id);
  }

  // return std::make_pair(alsapeer_id, networkpeer_id);
  return networkpeer_id;
}

void local_alsa_multi_listener_t::remove_alsa_connection(
    const aseq_t::port_t &port) {
  auto networkpeerI = aseqpeers.find(port);
  if (networkpeerI == aseqpeers.end()) {
    DEBUG("Removed ALSA port {}:{}, removing midipeer. NOT FOUND!", port.client,
          port.port);
    for (auto &peers : aseqpeers) {
      DEBUG("Known peer {}:{}", peers.first.port, peers.first.client);
    }
    return;
  }
  network_rtpmidi_listener_t *rtppeer =
      dynamic_cast<network_rtpmidi_listener_t *>(
          router->get_peer_by_id(networkpeerI->second).get());
  if (!rtppeer) {
    ERROR("Invalid router id is not a rtpmidiserverlistener!");
    return;
  }

  rtppeer->use_count--;

  INFO("One less user of peer: {}, use_count: {}", rtppeer->peer_id,
       rtppeer->use_count);
  if (rtppeer->use_count > 0) {
    return;
  }
  DEBUG("Removed ALSA port {}:{}, removing midipeer {}", port.client, port.port,
        networkpeerI->second);
  router->remove_peer(networkpeerI->second);
}

void local_alsa_multi_listener_t::alsaseq_event(snd_seq_event_t *event) {
  auto peerI =
      aseqpeers.find(aseq_t::port_t{event->source.client, event->source.port});
  if (peerI == aseqpeers.end()) {
    WARNING("Unknown source for event {}:{}!", event->source.client,
            event->source.port);
    for (auto &it : aseqpeers) {
      DEBUG("Known: {}:{}", it.first.client, it.first.port);
    }
    return;
  }
  rtpmidid::io_bytes_writer_static<1024> writer;
  alsatrans_decoder.ev_to_mididata(event, writer);
  auto midi = mididata_t(writer);

  router->send_midi(peer_id, peerI->second, midi);
}

void local_alsa_multi_listener_t::send_midi(midipeer_id_t from,
                                            const mididata_t &data) {
  for (auto &peer : aseqpeers) {
    // DEBUG("Look for dest alsa peer: {} == {} ? {}", peer.second, from,
    //       peer.second == from);
    if (peer.second == from) {
      auto mididata_copy =
          mididata_t(data); // Its just the pointers, not the data itself
      auto port = peer.first;
      alsatrans_encoder.mididata_to_evs_f(
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
json_t local_alsa_multi_listener_t::status() {
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
      {"type", "local:alsa:multi:listener"}, //
      {"name", name},                        //
      {"connections", connections}
      //
  };
}

} // namespace rtpmididns
