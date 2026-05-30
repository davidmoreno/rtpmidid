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

#include "peer_export_alsa_network.hpp"
#include "aseq.hpp"
#include "factory.hpp"
#include "peer_device_alsa_seq.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "peer_export_rtpmidi_server.hpp"
#include "peer_stable_id.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include <alsa/seqmid.h>
#include <memory>
#include <utility>

namespace rtpmididns {

peer_export_alsa_network_t::peer_export_alsa_network_t(
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
peer_export_alsa_network_t::~peer_export_alsa_network_t() {
  seq->remove_port(port);
}

midipeer_id_t
peer_export_alsa_network_t::new_alsa_connection(const aseq_t::port_t &port,
                                                 const std::string &name) {
  DEBUG("New connection to network peer {}, from a local connection to {}",
        name, this->name);

  midipeer_id_t networkpeer_id = MIDIPEER_ID_INVALID;
  router->for_each_peer<peer_export_rtpmidi_server_t>(
      [&](auto *peer) {
        if (peer->name_ == name) {
          peer->use_count++;
          networkpeer_id = peer->peer_id;
          aseqpeers[port] = networkpeer_id;
          DEBUG("One more user for peer: {}, count: {}", peer->peer_id,
                peer->use_count);
        }
      });

  if (networkpeer_id == MIDIPEER_ID_INVALID) {
    std::shared_ptr<midipeer_t> networkpeer =
        make_peer_export_rtpmidi_server(name, "");
    networkpeer_id = router->add_peer(networkpeer);

    aseqpeers[port] = networkpeer_id;
    router->connect(networkpeer_id, peer_id);
  }

  // return std::make_pair(alsapeer_id, networkpeer_id);
  return networkpeer_id;
}

void peer_export_alsa_network_t::remove_alsa_connection(
    const aseq_t::port_t &port) {
  auto networkpeerI = aseqpeers.find(port);
  if (networkpeerI == aseqpeers.end()) {
    DEBUG("Removed ALSA port {}:{}, removing midipeer. NOT FOUND!", port.client,
          port.port);
    for ([[maybe_unused]] auto &peers : aseqpeers) {
      DEBUG("Known peer {}:{}", peers.first.port, peers.first.client);
    }
    return;
  }
  auto midipeer = router->get_peer_by_id(networkpeerI->second).get();
  peer_export_rtpmidi_server_t *rtppeer =
      dynamic_cast<peer_export_rtpmidi_server_t *>(midipeer);
  if (!rtppeer) {
    ERROR("Invalid router id {} is not a rtpmidiserverlistener!",
          networkpeerI->second);
    if (midipeer == nullptr) {
      ERROR("It is a nullptr");
    } else {
      INFO("It is a {}", midipeer->get_type());
    }
    return;
  }

  rtppeer->use_count--;
  auto tracked_peer_id = networkpeerI->second;
  aseqpeers.erase(networkpeerI);

  INFO("One less user of peer: {}, use_count: {}", rtppeer->peer_id,
       rtppeer->use_count);
  if (rtppeer->use_count > 0) {
    return;
  }
  DEBUG("Removed ALSA port {}:{}, removing midipeer {}", port.client, port.port,
        tracked_peer_id);
  router->enqueue_remove_peer(tracked_peer_id);
}

void peer_export_alsa_network_t::alsaseq_event(snd_seq_event_t *event) {
  auto peerI =
      aseqpeers.find(aseq_t::port_t{event->source.client, event->source.port});
  if (peerI == aseqpeers.end()) {
    WARNING("Unknown source for event {}:{}!", event->source.client,
            event->source.port);
    for ([[maybe_unused]] auto &it : aseqpeers) {
      DEBUG("Known: {}:{}", it.first.client, it.first.port);
    }
    return;
  }
  rtpmidid::io_bytes_writer_static<1024> writer;
  const midipeer_id_t dest_network_peer = peerI->second;
  alsatrans_decoder.ev_to_mididata_f(
      event, writer, [&](const mididata_t &mididata) {
        if (!router) {
          WARNING("[MIDI_FLOW] peer {}: No router, cannot forward ALSA MIDI",
                  peer_id);
          return;
        }
        // Directed send: multi-listener has no broadcast send_to from RTP peers.
        router->enqueue_send_midi(peer_id, dest_network_peer, mididata);
      });
}

void peer_export_alsa_network_t::send_midi(midipeer_id_t from,
                                            const mididata_t &data) {
  for (auto &peer : aseqpeers) {
    if (peer.second == from) {
      auto mididata_copy =
          mididata_t(data); // Its just the pointers, not the data itself
      auto port = peer.first;
      std::scoped_lock lock(seq->output_mutex);
      alsatrans_encoder.mididata_to_evs_f(
          mididata_copy, [this, port](snd_seq_event_t *ev) {
            snd_seq_ev_set_source(ev, this->port);
            snd_seq_ev_set_dest(ev, port.client, port.port);
            snd_seq_ev_set_direct(ev);
            auto result = snd_seq_event_output(seq->seq, ev);
            if (result < 0) {
              ERROR("Error sending from={} to={}: {}", this->port, port,
                    snd_strerror(result));
              snd_seq_drop_input(seq->seq);
              snd_seq_drop_output(seq->seq);
              return;
            }
            result = snd_seq_drain_output(seq->seq);
            if (result < 0) {
              ERROR("Error drain sending from={} to={}: {}", this->port, port,
                    snd_strerror(result));
              snd_seq_drop_input(seq->seq);
              snd_seq_drop_output(seq->seq);
            }
          });
    }
  }
}
router_peer_row_t peer_export_alsa_network_t::status() const {
  router_peer_row_t row;
  std::vector<alsa_connection_item_t> connections;
  for (const auto &peer : aseqpeers) {
    const auto &port = peer.first;
    alsa_connection_item_t it;
    it.alsa = FMT::format("{}:{}", port.client, port.port);
    it.local = std::to_string(peer.second);
    connections.push_back(std::move(it));
  }
  row.name = name;
  row.connections = std::move(connections);
  return row;
}

std::optional<std::string>
peer_export_alsa_network_t::stable_id_from_row(const router_peer_row_t &row) {
  const std::string peer_name =
      row.name && !row.name->empty() ? *row.name : std::string();
  if (peer_name.empty())
    return std::nullopt;
  return make_stable_id("alsa_multi", {peer_name});
}

std::optional<std::string>
peer_export_alsa_network_t::compute_stable_id_impl() const {
  return stable_id_from_row(status());
}

} // namespace rtpmididns
