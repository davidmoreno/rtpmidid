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
#include "rtpmidid/utils.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace rtpmidid {
class mdns_rtpmidi_t;
}

namespace rtpmididns {
class connection_db_manager_t;

class control_socket_t {
  NON_COPYABLE_NOR_MOVABLE(control_socket_t)

  std::thread server_thread_;
  std::atomic<bool> server_running_{false};

  void server_thread_main();
  /** @return true if @p fd should be closed and removed from the poll set */
  bool handle_client_data(int fd);

public:
  int socket = -1;
  time_t start_time = 0;
  std::shared_ptr<midirouter_t> router = nullptr;
  std::shared_ptr<aseq_t> aseq = nullptr;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns = nullptr;
  std::shared_ptr<connection_db_manager_t> connection_db;

  control_socket_t();
  ~control_socket_t() noexcept;

  /** Stop the server thread and close the listening socket (idempotent). */
  void stop();

  std::string parse_command(const std::string &command);
};
} // namespace rtpmididns
