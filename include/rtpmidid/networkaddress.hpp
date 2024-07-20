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

private:
  const sockaddr *addr;
  socklen_t len;
  bool managed = false; // If managed, release memory on destruction

public:
  network_address_t(const sockaddr *addr, socklen_t len)
      : addr(addr), len(len){};
  network_address_t(sockaddr_storage *addr, socklen_t len)
      : addr(sockaddr_storage_to_sockaddr(addr)), len(len){};
  network_address_t(int fd);
  network_address_t() : addr(nullptr), len(0){};
  network_address_t(network_address_t &&other) {
    addr = other.addr;
    len = other.len;
    managed = other.managed;
    other.managed = false;
    other.addr = nullptr;
    other.len = 0;
  }
  void operator=(network_address_t &&other) {
    if (addr && managed) {
      delete (sockaddr_storage *)addr;
    }

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
  sockaddr const *get_address() const { return addr; }
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

  bool is_valid() const { return addr != nullptr; }

  // resolve all the possible addresses for a given address and port, until
  // return true, return false if no address pass the loop successfully.
  static bool resolve_loop(const std::string &address, const std::string &port,
                           std::function<bool(const network_address_t &)> cb);
  static bool resolve_loop(const std::string &address, const std::string &port,
                           std::function<bool(const addrinfo *)> cb);
};

class network_address_list_t {
  NON_COPYABLE(network_address_list_t);
  addrinfo *info = nullptr;

public:
  network_address_list_t() { info = nullptr; };
  network_address_list_t(const std::string &name, const std::string &port);
  ~network_address_list_t();

  network_address_list_t &operator=(network_address_list_t &&other) {
    if (info) {
      freeaddrinfo(info);
    }
    info = other.info;
    other.info = nullptr;
    return *this;
  }

  network_address_t get_first() const {
    if (!is_valid()) {
      return network_address_t();
    }
    return network_address_t(info->ai_addr, info->ai_addrlen)
        .dup(); // dup ensure managed
  }

  bool is_valid() const { return info != nullptr; }

  class iterator_t {
    addrinfo *info = nullptr;

  public:
    iterator_t(){};
    iterator_t(addrinfo *info) : info(info){};
    // iterator_t(network_address_list_t other) : info(other.info){};
    iterator_t(const network_address_list_t &other) : info(other.info){};
    iterator_t &operator++() {
      info = info->ai_next;
      return *this;
    }
    iterator_t operator++(int) {
      iterator_t tmp = *this;
      ++*this;
      return tmp;
    }

    bool operator!=(const iterator_t &other) const {
      return info != other.info;
    }
    const network_address_t operator*() const {
      return network_address_t(info->ai_addr, info->ai_addrlen);
    }
  };
  friend iterator_t;

  iterator_t begin() const { return iterator_t(this->info); }
  iterator_t end() const { return iterator_t(nullptr); }
};

} // namespace rtpmidid