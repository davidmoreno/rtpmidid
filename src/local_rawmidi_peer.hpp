/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "midi_normalizer.hpp"
#include "midipeer.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/utils.hpp"
#include <string>

namespace rtpmididns {
/**
 * @short ALSA port that just receives data and send to another midipeer_t
 */
class local_rawmidi_peer_t : public midipeer_t {
  NON_COPYABLE_NOR_MOVABLE(local_rawmidi_peer_t);

public:
  std::string device;
  std::string name;
  int fd = -1;
  rtpmidid::poller_t::listener_t fd_listener;
  midi_normalizer_t midi_normalizer;
  int connection_count = 0;

  local_rawmidi_peer_t(const std::string &name, const std::string &device);
  ~local_rawmidi_peer_t() override;

  void read_midi();
  void open();
  void close();

  json_t status() override;
  void send_midi(midipeer_id_t from, const mididata_t &) override;
  void event(midipeer_event_e event, midipeer_id_t from) override;
  void connected(midipeer_id_t peer_id);
  void disconnected(midipeer_id_t peer_id);
  const char *get_type() const override { return "local_rawmidi_peer_t"; }
};
} // namespace rtpmididns
