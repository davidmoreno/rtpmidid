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
#include "midirouter.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/utils.hpp"
#include <string>
#include <vector>

namespace rtpmidid {
class mdns_rtpmidi_t;
}

namespace rtpmididns {
class control_socket_t {
  NON_COPYABLE_NOR_MOVABLE(control_socket_t)

  struct client_t {
    int fd = -1;
    rtpmidid::poller_t::listener_t listener;
  };

public:
  int socket;
  std::vector<client_t> clients;
  rtpmidid::poller_t::listener_t connection_listener;
  time_t start_time;
  std::shared_ptr<midirouter_t> router = nullptr;
  std::shared_ptr<aseq_t> aseq = nullptr;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns = nullptr;

public:
  control_socket_t();
  ~control_socket_t() noexcept;

  void connection_ready();
  void data_ready(int fd);
  std::string parse_command(const std::string &command);
};
} // namespace rtpmididns
