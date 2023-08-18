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
#include "rtpmidid/iobytes.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace rtpmididns {
class mididata_t;
class midipeer_t;

using peer_id_t = uint32_t;

struct peerconnection_t {
  uint32_t id;
  std::shared_ptr<midipeer_t> peer;
  std::vector<peer_id_t> send_to;
};

class mididata_t : public rtpmidid::io_bytes_reader {
public:
  mididata_t(uint8_t *data, uint32_t size)
      : rtpmidid::io_bytes_reader(data, size) {}
};

class midirouter_t {
public:
  peer_id_t max_id;
  std::unordered_map<uint32_t, peerconnection_t> peers;

  peer_id_t add_peer(std::shared_ptr<midipeer_t>);
  void connect(peer_id_t from, peer_id_t to);

  void send_midi(peer_id_t from, const mididata_t &data);
};
} // namespace rtpmididns
