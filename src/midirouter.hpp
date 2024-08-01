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
#include "json_fwd.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/utils.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace rtpmididns {
class mididata_t;
class midipeer_t;

using peer_id_t = uint32_t;

struct peerconnection_t {
  uint32_t id = 0;
  std::shared_ptr<midipeer_t> peer;
  std::vector<peer_id_t> send_to{};
};

class midirouter_t : public std::enable_shared_from_this<midirouter_t> {
  NON_COPYABLE_NOR_MOVABLE(midirouter_t)

public:
  peer_id_t max_id = 1;
  std::unordered_map<uint32_t, peerconnection_t> peers;
  midirouter_t();
  ~midirouter_t();

  peer_id_t add_peer(std::shared_ptr<midipeer_t>);
  std::shared_ptr<midipeer_t> get_peer_by_id(peer_id_t peer_id);
  peerconnection_t *get_peerdata_by_id(peer_id_t peer_id);

  void remove_peer(peer_id_t);
  void connect(peer_id_t from, peer_id_t to);
  void peer_connection_loop(peer_id_t peer_id,
                            std::function<void(std::shared_ptr<midipeer_t>)>);

  void send_midi(peer_id_t from, const mididata_t &data);
  void send_midi(peer_id_t from, peer_id_t to, const mididata_t &data);

  // To force clear the peers and avoid the cyclic references of peers that keep
  // the router.
  void clear();

  json_t status();

  // For the given type of the for_each, by default midipeer_t.
  template <typename T = midipeer_t>
  void for_each_peer(const std::function<void(T *)> &f) {
    for (auto &[peer_id, peer] : peers) {
      auto t = dynamic_cast<T *>(peer.peer.get());
      if (t)
        f(t);
    }
  };
};
} // namespace rtpmididns
