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
#include "rtpmidid/utils.hpp"
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
class local_alsa_multi_listener_t : public midipeer_t {
  NON_COPYABLE_NOR_MOVABLE(local_alsa_multi_listener_t);

public:
  std::shared_ptr<aseq_t> seq;
  uint8_t port;
  mididata_to_alsaevents_t alsatrans_decoder;
  mididata_to_alsaevents_t alsatrans_encoder;
  std::string name;

  std::unordered_map<aseq_t::port_t, midipeer_id_t> aseqpeers;
  rtpmidid::connection_t<aseq_t::port_t, const std::string &>
      subscribe_connection;
  rtpmidid::connection_t<aseq_t::port_t> unsubscribe_connection;
  rtpmidid::connection_t<snd_seq_event_t *> midi_connection;

  local_alsa_multi_listener_t(const std::string &name,
                              std::shared_ptr<aseq_t> aseq);
  ~local_alsa_multi_listener_t() override;

  void send_midi(midipeer_id_t from, const mididata_t &) override;
  json_t status() override;

  // Returns the RTPSERVER id. Useful for testing.
  midipeer_id_t new_alsa_connection(const aseq_t::port_t &port,
                                    const std::string &name);
  void remove_alsa_connection(const aseq_t::port_t &port);

  // received data from the alsa side, look who is the aseqpeer_t
  // and send pretending its it.
  void alsaseq_event(snd_seq_event_t *event);
};

} // namespace rtpmididns
