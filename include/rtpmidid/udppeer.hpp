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

public:
  // Read some data from address and port
  signal_t<io_bytes_reader &, const network_address_t &> on_read;

  udppeer_t() { open("::", "0"); };
  udppeer_t(const std::string &address, const std::string &port) {
    open(address, port);
  }
  udppeer_t(const network_address_t &addr);
  udppeer_t(const sockaddr *addr, socklen_t socklen)
      : udppeer_t(network_address_t(addr, socklen)){};
  udppeer_t(udppeer_t &&other) {
    fd = other.fd;
    listener = std::move(other.listener);
    addresses_cache = std::move(other.addresses_cache);
    other.fd = -1;
  }
  void operator=(udppeer_t &&other){
    fd = other.fd;
    listener = std::move(other.listener);
    addresses_cache = std::move(other.addresses_cache);
    other.fd=-1;
  }

  ~udppeer_t() { close(); }

  int open(const std::string &address, const std::string &port);
  void send(io_bytes &reader, const std::string &address,
            const std::string &port);
  void close();

  bool is_open() const { return fd >= 0; }
  network_address_t get_address();

private:
  int fd = -1;
  poller_t::listener_t listener;
  std::map<std::pair<std::string, std::string>, network_address_t>
      addresses_cache;

  void data_ready();

  network_address_t const *get_address(const std::string &address,
                                       const std::string &port);
};

} // namespace rtpmidid
