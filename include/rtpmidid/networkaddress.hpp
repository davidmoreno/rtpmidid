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

#include "rtpmidid/network.hpp"
#include "rtpmidid/utils.hpp"
#include <functional>
#include <stddef.h>
#include <string>
#include <sys/socket.h>

namespace rtpmidid {
class network_address_t {
  NON_COPYABLE(network_address_t);

public:
  network_address_t(sockaddr *addr, size_t len) : addr(addr), len(len){};
  network_address_t(sockaddr_storage *addr, size_t len)
      : addr(sockaddr_storage_to_sockaddr(addr)), len(len){};
  network_address_t(int fd);
  network_address_t(network_address_t &&other) {
    addr = other.addr;
    len = other.len;
    managed = other.managed;
    other.managed = false;
    other.addr = nullptr;
    other.len = 0;
  }
  ~network_address_t();

  int port() const;
  std::string ip() const;
  std::string hostname() const;
  std::string to_string() const;
  sockaddr *get_address() const { return addr; }
  network_address_t dup() const {
    sockaddr_storage *addr = new sockaddr_storage();
    memcpy(addr, this->addr, this->len);
    auto ret = network_address_t(addr, len);
    ret.managed = true;
    return ret;
  }
  sockaddr const *get_sockaddr() const { return addr; }
  socklen_t get_socklen() const { return len; }
  int get_aifamily() const { return addr->sa_family; }

  // resolve all the possible addresses for a given address and port, until
  // return true, return false if no address pass the loop successfully.
  static bool resolve_loop(const std::string &address, const std::string &port,
                           std::function<bool(const network_address_t &)> cb);
  static bool resolve_loop(const std::string &address, const std::string &port,
                           std::function<bool(const addrinfo *)> cb);

private:
  sockaddr *addr;
  socklen_t len;
  bool managed = false; // If managed, release memory on destruction
};

} // namespace rtpmidid