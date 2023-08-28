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
#pragma once

#include "aseq.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/rtppeer.hpp"
#include <unordered_map>

namespace rtpmididns {

/**
 * @short Just the exported Network entry point (`ALSA / Network`)
 *
 * This is the ALSA `Network` port, which has these functionalities:
 *
 * * New ALSA connections create a rtpmidid server port:
 *   * Data coming from that ALSA port goes to this rtpmidid server
 *   * Data from this rtpmidid server goes to this ALSA port
 *
 * The way to do it using the midirouter is when we get a new ALSA
 * midi connection, we create the rtpmidid server (rtpmididpeer_t)
 * and connect them.
 *
 * When the ALSA port receives ALSA sequencer data, we check the
 * origin port and use that port to match to the ALSA connection
 * and send the data as if it comes from there to the midirouter.
 */
class alsanetwork_t : public midipeer_t {
public:
  std::shared_ptr<rtpmidid::aseq> seq;
  uint8_t port;
  rtpmidid::mididata_to_alsaevents_t alsatrans_decoder;
  rtpmidid::mididata_to_alsaevents_t alsatrans_encoder;

  std::unordered_map<rtpmidid::aseq::port_t, midipeer_id_t> aseqpeers;
  connection_t<rtpmidid::aseq::port_t, const std::string &>
      subscribe_connection;
  connection_t<rtpmidid::aseq::port_t> unsubscibe_connection;
  connection_t<snd_seq_event_t *> midi_connection;

  alsanetwork_t(const std::string &name);
  ~alsanetwork_t();

  void send_midi(midipeer_id_t from, const mididata_t &) override;
  json_t status() override;

  // Returns the RTPSERVER id. Useful for testing.
  midipeer_id_t new_alsa_connection(const rtpmidid::aseq::port_t &port,
                                    const std::string &name);
  void remove_alsa_connection(const rtpmidid::aseq::port_t &port);

  // received data from the alsa side, look who is the aseqpeer_t
  // and send pretending its it.
  void alsaseq_event(snd_seq_event_t *event);
};

} // namespace rtpmididns
