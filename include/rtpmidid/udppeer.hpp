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

#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/network.hpp"
#include "rtpmidid/networkaddress.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/signal.hpp"
#include "rtpmidid/utils.hpp"
#include <stddef.h>
#include <string>
#include <sys/socket.h>

namespace rtpmidid {

class udppeer_t {
  NON_COPYABLE(udppeer_t);

private:
  int fd = -1;
  poller_t::listener_t listener;

  void data_ready();

  network_address_t const *get_address(const std::string &address,
                                       const std::string &port);

public:
  // Read some data from address and port
  signal_t<io_bytes_reader &, const network_address_t &> on_read;

  udppeer_t() { fd = -1; };
  udppeer_t(const std::string &address, const std::string &port) {
    open(network_address_list_t(address, port));
  }
  udppeer_t(const network_address_t &addr);
  udppeer_t(const network_address_list_t &addrlist) { open(addrlist); };
  udppeer_t(const sockaddr *addr, socklen_t socklen)
      : udppeer_t(network_address_t(addr, socklen)){};
  udppeer_t(udppeer_t &&other) {
    fd = other.fd;
    listener = std::move(other.listener);
    other.fd = -1;
  }
  void operator=(udppeer_t &&other) {
    fd = other.fd;
    listener = std::move(other.listener);
    other.fd = -1;
  }

  ~udppeer_t() { close(); }

  int open(const network_address_t &addr);
  int open(const network_address_list_t &addr);
  ssize_t sendto(const io_bytes &reader, const network_address_t &address);
  void close();

  bool is_open() const { return fd >= 0; }
  network_address_t get_address();
};

} // namespace rtpmidid
