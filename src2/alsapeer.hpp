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

namespace rtpmididns {
class alsapeer_t : public midipeer_t {
public:
  uint8_t port;
  std::shared_ptr<rtpmidid::aseq> seq;

  connection_t<rtpmidid::aseq::port_t, const std::string &>
      subscribe_connection;
  connection_t<rtpmidid::aseq::port_t> unsubscibe_connection;
  connection_t<snd_seq_event_t *> midi_connection;

  alsapeer_t(const std::string &name, std::shared_ptr<rtpmidid::aseq> seq);
  virtual ~alsapeer_t();
  virtual void send_midi(midipeer_id_t from, const mididata_t &) override;
};
} // namespace rtpmididns
