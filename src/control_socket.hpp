/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#include <string>
#include <vector>

namespace rtpmidid {
class rtpmidid_t;

/**
 * @short Opens a control socket that receives commands to control the rtpmidid
 *
 * See the /CONTROL.md file for known commands and protocol.
 */
class control_socket_t {
  time_t start_time;
  int listen_socket;
  std::vector<int> clients;
  rtpmidid_t &rtpmidid;

public:
  control_socket_t(rtpmidid::rtpmidid_t &rtpmidid, const std::string &filename);
  ~control_socket_t();
  void connection_ready();
  void data_ready(int fd);
  std::string parse_command(const std::string &);
};
} // namespace rtpmidid
