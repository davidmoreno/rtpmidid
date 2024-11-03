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

#include <cassert>
#include <functional>
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/networkaddress.hpp>

namespace rtpmidid {

network_address_t::network_address_t(int fd) {
  auto addr = sockaddr_storage_to_sockaddr(new sockaddr_storage());
  len = sizeof(sockaddr_storage);
  managed = true;

  int res;
  res = ::getsockname(fd, addr, &len);
  if (res < 0) {
    throw rtpmidid::exception("Error getting info the newly created midi "
                              "socket. Can not create server.");
  }
  this->addr = addr;
}

network_address_t::~network_address_t() {
  if (managed) {
    delete (sockaddr_storage *)addr;
  }
}

int network_address_t::port() const {
  if (!addr) {
    ERROR("This network address do not point to any address.");
    return 0;
  }
  if (addr->sa_family == AF_INET) {
    return ntohs(reinterpret_cast<const sockaddr_in *>(addr)->sin_port);
  }
  return ntohs(reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_port);
}

std::string network_address_t::ip() const {
  if (!addr) {
    ERROR("This network address do not point to any address.");
    return "null";
  }
  std::array<char, INET6_ADDRSTRLEN> name{};
  if (addr->sa_family == AF_INET) {
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr,
              name.data(), name.size());
    return name.data();
  }
  inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr,
            name.data(), name.size());
  return name.data();
}

std::string network_address_t::hostname() const {
  if (!addr) {
    return "null";
  }
  std::array<char, NI_MAXHOST> name{};
  if (getnameinfo(addr, len, name.data(), NI_MAXHOST, nullptr, 0, 0) != 0) {
    return ip();
  }
  return name.data();
}

std::string network_address_t::to_string() const {
  if (!addr) {
    return "null";
  }

  std::array<char, INET6_ADDRSTRLEN> name{};
  if (addr->sa_family == AF_INET) {
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr,
              name.data(), name.size());
    return FMT::format(
        "{}:{}", name.data(),
        ntohs(reinterpret_cast<const sockaddr_in *>(addr)->sin_port));
  }
  inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr,
            name.data(), name.size());
  return FMT::format(
      "{}:{}", name.data(),
      ntohs(reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_port));
}

bool network_address_t::resolve_loop(
    const std::string &address, const std::string &port,
    std::function<bool(const network_address_t &)> cb) {

  return network_address_t::resolve_loop(
      address, port, [&cb](const addrinfo *addr) {
        network_address_t address{addr->ai_addr, addr->ai_addrlen};
        return cb(address);
      });
}

bool network_address_t::resolve_loop(const std::string &address,
                                     const std::string &port,
                                     std::function<bool(const addrinfo *)> cb) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = nullptr;
  hints.ai_addr = nullptr;
  hints.ai_next = nullptr;

  addrinfo *result;
  if (getaddrinfo(address.c_str(), port.c_str(), &hints, &result) != 0) {
    return false;
  }

  for (auto *addr = result; addr != nullptr; addr = addr->ai_next) {
    network_address_t address{addr->ai_addr, addr->ai_addrlen};
    if (cb(addr)) {
      freeaddrinfo(result);
      return true;
    }
  }
  freeaddrinfo(result);
  return false;
}

void network_address_t::set_port(int port) {
  if (!addr) {
    ERROR("This network address do not point to any address.");
    return;
  }
  assert(managed); // Only managed addresses can be modified.

  sockaddr *addr = const_cast<sockaddr *>(this->addr);

  if (addr->sa_family == AF_INET) {
    reinterpret_cast<sockaddr_in *>(addr)->sin_port = htons(port);
  } else {
    reinterpret_cast<sockaddr_in6 *>(addr)->sin6_port = htons(port);
  }
}

network_address_list_t::network_address_list_t() : info(nullptr) {}

network_address_list_t::network_address_list_t(const std::string &name,
                                               const std::string &port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = nullptr;
  hints.ai_addr = nullptr;
  hints.ai_next = nullptr;

  if (getaddrinfo(name.c_str(), port.c_str(), &hints, &info) != 0) {
    ERROR("Error getting address info for {}:{}", name, port);
    if (info) {
      freeaddrinfo(info);
    }
    info = nullptr;
  }
}

network_address_list_t::~network_address_list_t() {
  if (info) {
    freeaddrinfo(info);
  }
}

network_address_list_t &
network_address_list_t::operator=(network_address_list_t &&other) {
  if (info) {
    freeaddrinfo(info);
  }
  info = other.info;
  other.info = nullptr;
  return *this;
}

network_address_t network_address_list_t::get_first() const {
  if (info == nullptr) {
    return network_address_t{};
  }
  return network_address_t{info->ai_addr, info->ai_addrlen};
}

std::string network_address_list_t::to_string() const {
  if (info == nullptr) {
    return "null";
  }
  std::string result = "{ ";
  for (auto address : *this) {
    result += address.to_string() + ", ";
  }
  result += "}";
  return result;
}

} // namespace rtpmidid
