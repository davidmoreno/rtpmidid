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

#include "alsaworker.hpp"
#include "aseq.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"

using namespace rtpmididns;

alsaworker_t::alsaworker_t(const std::string &name_,
                           std::shared_ptr<aseq_t> seq_)
    : seq(seq_), name(name_) {
  port = seq->create_port(name);
  INFO("Created alsapeer {}, port {}", name, port);

  midi_connection = seq->midi_event[port].connect([this](snd_seq_event *ev) {
    rtpmidid::io_bytes_static<1024> data;
    auto datawriter = rtpmidid::io_bytes_writer(data);
    mididata_decoder.decode(ev, datawriter);
    auto mididata = mididata_t(datawriter);
    router->send_midi(peer_id, mididata);
  });
}

alsaworker_t::~alsaworker_t() { seq->remove_port(port); }

void alsaworker_t::send_midi(midipeer_id_t from, const mididata_t &data) {
  packets_recv += 1;
  auto readerdata = rtpmidid::io_bytes_reader(data);
  mididata_encoder.encode(readerdata, [this](snd_seq_event_t *ev) {
    snd_seq_ev_set_source(ev, this->port);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    snd_seq_event_output_direct(seq->seq, ev);
  });
}

json_t alsaworker_t::status() {
  return json_t{
      {"type", "alsa_worker"}, {"name", name},
      //
  };
}