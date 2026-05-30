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

#include "test_fake_peer.hpp"

#include "../src/peer_kind.hpp"

namespace rtpmididns {

fake_alsa_seq_peer_t::fake_alsa_seq_peer_t(std::string name,
                                           std::string client,
                                           std::string port)
    : name_(std::move(name)), client_(std::move(client)),
      port_(std::move(port)) {}

const char *fake_alsa_seq_peer_t::get_type() const {
  return peer_kind_wire_type(peer_kind_e::device_alsa_seq);
}

router_peer_row_t fake_alsa_seq_peer_t::status() const {
  router_peer_row_t row;
  row.type = get_type();
  row.name = name_;
  alsa_subscribe_from_t sub;
  sub.client_name = client_;
  sub.port_name = port_;
  row.alsa_subscribe_from = sub;
  return row;
}

} // namespace rtpmididns
