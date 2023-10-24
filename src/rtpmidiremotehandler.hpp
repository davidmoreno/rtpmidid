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
#include "rtpmidid/signal.hpp"
#include <memory>
#include <vector>

namespace rtpmididns {
class midirouter_t;
class aseq_t;
class midipeer_t;

/**
 *
 */
class rtpmidi_remote_handler_t {
public:
  // When discover new peers, if same name, go to the same peer
  struct known_remote_peer_t {
    std::string name;
    std::shared_ptr<midipeer_t> alsawaiter;
  };

  connection_t<const std::string &, const std::string &, const std::string &>
      discover_connection;
  connection_t<const std::string &, const std::string &, const std::string &>
      remove_connection;

  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<aseq_t> aseq;
  std::vector<known_remote_peer_t> peers;

  rtpmidi_remote_handler_t(std::shared_ptr<midirouter_t>,
                           std::shared_ptr<aseq_t>);

  void discover_peer(const std::string &name, const std::string &hostname,
                     const std::string &port);
  void remove_peer(const std::string &name, const std::string &hostname,
                   const std::string &port);
};
} // namespace rtpmididns
