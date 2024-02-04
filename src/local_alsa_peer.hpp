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
#include "rtpmidid/utils.hpp"

namespace rtpmididns {
/**
 * @short ALSA port that just receives data and send to another midipeer_t
 */
class local_alsa_peer_t : public midipeer_t {
  NON_COPYABLE_NOR_MOVABLE(local_alsa_peer_t);

public:
  // If empty port, do not check that side of the message
  aseq_t::connection_t conn;

  std::shared_ptr<aseq_t> seq;
  std::string name;
  mididata_to_alsaevents_t mididata_encoder;
  mididata_to_alsaevents_t mididata_decoder;

  rtpmidid::connection_t<aseq_t::port_t, const std::string &>
      subscribe_connection;
  rtpmidid::connection_t<aseq_t::port_t> unsubscribe_connection;
  rtpmidid::connection_t<snd_seq_event_t *> midi_connection;

  // Mode 1. Create a new port and listen there
  local_alsa_peer_t(const std::string &name, std::shared_ptr<aseq_t> seq);
  ~local_alsa_peer_t() override;
  // Mode 2. Port already exists
  local_alsa_peer_t(const std::string &name, aseq_t::connection_t conn,
                    std::shared_ptr<aseq_t> seq);

  // To be called after port, seq and name are properly set
  void initialize();

  json_t status() override;
  void send_midi(midipeer_id_t from, const mididata_t &) override;
};
} // namespace rtpmididns
