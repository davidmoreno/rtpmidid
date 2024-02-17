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

#include "local_alsa_peer.hpp"
#include "aseq.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"

using namespace rtpmididns;

local_alsa_peer_t::local_alsa_peer_t(const std::string &name_,
                                     std::shared_ptr<aseq_t> seq_)
    : seq(seq_), name(name_) {

  auto my_port = seq->create_port(name);
  conn = std::move(aseq_t::connection_t(
      seq, aseq_t::port_t{seq->client_id, my_port}, aseq_t::port_t{}));

  INFO("Created alsapeer {}, conn {}", name, conn.to_string());

  initialize();
}

local_alsa_peer_t::local_alsa_peer_t(const std::string &name_,
                                     aseq_t::connection_t conn_,
                                     std::shared_ptr<aseq_t> seq_)
    : conn(std::move(conn_)), seq(seq_), name(name_) {
  initialize();
}

void local_alsa_peer_t::initialize() {
  DEBUG("Listening to conn {}", conn.to_string());
  midi_connection = seq->midi_event[conn.get_my_port().port].connect(
      [this](snd_seq_event *ev) { alsaseq_event(ev); });
}

void local_alsa_peer_t::alsaseq_event(snd_seq_event *ev) {
  //        DEBUG("MIDI EVENT RECEIVED, port {}", conn.to_string());
  auto &source = ev->source;
  auto &dest = ev->dest;
  if (router->debug) {
    DEBUG("Listening events with this source {} and dest {}", conn.from,
          conn.to);
    DEBUG("Received event {{{}:{} -> {}:{}}}, type: {}", source.client,
          source.port, dest.client, dest.port, ev->type);
  }

  if (auto their_port = conn.get_their_port(); !their_port.is_empty()) {
    auto ev_from = aseq_t::port_t{source.client, source.port};
    if (ev_from != their_port) {
      if (router->debug)
        DEBUG("Not for me! {} != {}", ev_from.to_string(),
              their_port.to_string());
      return;
    }
  }
  if (auto my_port = conn.get_my_port(); !my_port.is_empty()) {
    auto ev_to = aseq_t::port_t{dest.client, dest.port};
    if (ev_to != my_port) {
      if (router->debug)
        DEBUG("Not for me! {} != {}", ev_to.to_string(), my_port.to_string());
      return;
    }
  }

  //    DEBUG("For me!");

  rtpmidid::io_bytes_static<1024> data;
  auto datawriter = rtpmidid::io_bytes_writer(data);
  mididata_decoder.ev_to_mididata(ev, datawriter);
  auto mididata = mididata_t(datawriter);
  router->send_midi(peer_id, mididata);
}

local_alsa_peer_t::~local_alsa_peer_t() {
  auto my_port = conn.get_my_port();
  if (!my_port.empty()) {
    seq->remove_port(my_port.port);
  }
}

void local_alsa_peer_t::send_midi(midipeer_id_t from, const mididata_t &data) {
  if (router->debug) {
    DEBUG("Sending data to ALSA. {} bytes, my conn is: {}", data.size(), conn);
  }

  packets_recv += 1;
  auto readerdata = rtpmidid::io_bytes_reader(data);
  mididata_encoder.mididata_to_evs_f(readerdata, [this](snd_seq_event_t *ev) {
    auto my_port = conn.get_my_port();
    if (!my_port.is_empty()) {
      snd_seq_ev_set_source(ev, my_port.port);
    }
    auto other_port = conn.get_their_port();
    if (!other_port.is_empty()) {
      snd_seq_ev_set_dest(ev, other_port.client, other_port.port);
    } else {
      snd_seq_ev_set_subs(ev); // to all subscribers
    }

    if (router->debug) {
      DEBUG("Sending event {{{}:{} -> {}:{}}}, type: {}", ev->source.client,
            ev->source.port, ev->dest.client, ev->dest.port, ev->type);
    }

    snd_seq_ev_set_direct(ev);
    snd_seq_event_output_direct(seq->seq, ev);
  });
}

json_t local_alsa_peer_t::status() {
  return json_t{
      {"type", "local:alsa:peer"}, //
      {"name", name},
      {"connection",
       {
           {"from", {{"client", conn.from.client}, {"port", conn.from.port}}},
           {"to", {{"client", conn.to.client}, {"port", conn.to.port}}},
       }},
      //
  };
}