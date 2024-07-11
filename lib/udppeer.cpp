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

#include "rtpmidid/udppeer.hpp"
#include "rtpmidid/network.hpp"
#include "rtpmidid/poller.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace rtpmidid {

const auto MAX_ADDRESS_CACHE_SIZE = 100;

int udppeer_t::open(const std::string &address, const std::string &port) {
  struct addrinfo *sockaddress_list = nullptr;
  struct addrinfo hints {};

  int res = -1;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  const char *cport = port.c_str();
  const char *caddress = address.c_str();

  res = getaddrinfo(caddress, cport, &hints, &sockaddress_list);
  if (res < 0) {
    DEBUG("Error resolving address {}:{}", "::", port);
    throw rtpmidid::exception("Can not resolve address {}:{}. {}", "::", port,
                              strerror(errno));
  }
  // Get addr info may return several options, try them in order.
  // we asume that if the control success to be created the midi will too.
  std::array<char, NI_MAXHOST> host{};
  std::array<char, NI_MAXHOST> service{};
  socklen_t peer_addr_len = NI_MAXHOST;
  auto listenaddr = sockaddress_list;
  for (; listenaddr != nullptr; listenaddr = listenaddr->ai_next) {
    host[0] = service[0] = 0x00;
    getnameinfo(listenaddr->ai_addr, peer_addr_len, host.data(), NI_MAXHOST,
                service.data(), NI_MAXSERV, NI_NUMERICSERV);
    DEBUG("Try listen at {}:{}", host.data(), service.data());

    fd = socket(listenaddr->ai_family, listenaddr->ai_socktype,
                listenaddr->ai_protocol);
    if (fd < 0) {
      continue; // Bad socket. Try next.
    }
    if (bind(fd, listenaddr->ai_addr, listenaddr->ai_addrlen) == 0) {
      break;
    }
    ::close(fd);
    fd = -1;
  }
  if (!listenaddr) {
    throw rtpmidid::exception("Can not open udp socket. {}.", strerror(errno));
  }
  struct sockaddr_storage addr {};
  unsigned int len = sizeof(addr);
  res = ::getsockname(fd, sockaddr_storage_to_sockaddr(&addr), &len);
  if (res < 0) {
    throw rtpmidid::exception("Error getting info the newly created midi "
                              "socket. Can not create server.");
  }
  if (listenaddr->ai_family == AF_INET) {
    auto *addr4 = reinterpret_cast<struct sockaddr_in *>(&addr);
    this->port = ntohs(addr4->sin_port);
  } else {
    auto *addr6 = reinterpret_cast<struct sockaddr_in6 *>(&addr);
    this->port = ntohs(addr6->sin6_port);
  }
  this->address = host.data();

  listener = poller.add_fd_in(fd, [this](int fd) {
    assert(fd == this->fd); // Should be the same.
    data_ready();
  });

  freeaddrinfo(sockaddress_list);

  DEBUG("UDP port listen ready, at {}:{}, fd {}", this->address, this->port,
        fd);

  return res;
}

void udppeer_t::data_ready() {
  std::array<uint8_t, 1500> raw{};
  struct sockaddr_storage cliaddr {};
  unsigned int len = sizeof(cliaddr);
  auto n = recvfrom(fd, raw.data(), 1500, MSG_DONTWAIT,
                    sockaddr_storage_to_sockaddr(&cliaddr), &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0) {
    throw exception("Error reading at {}:{}", address, port);
  }

  auto buffer = io_bytes_reader(raw.data(), n);
  std::array<char, NI_MAXHOST> host{};
  std::array<char, NI_MAXSERV> service{};

  getnameinfo(sockaddr_storage_to_sockaddr(&cliaddr), len, host.data(),
              NI_MAXHOST, service.data(), NI_MAXSERV, NI_NUMERICSERV);

  std::string remote_address = std::string(host.data());
  int remote_port = std::stoi(service.data());
  DEBUG("Got data from {}:{}, {} bytes", remote_address, remote_port, n);

  on_read(buffer, remote_address, remote_port);
}

void udppeer_t::send(io_bytes &buffer, const std::string &address,
                     const std::string &port) {

  auto addr = get_address(address, port);

  auto res =
      sendto(fd, buffer.start, buffer.size(), 0, &(addr->addr), addr->len);

  if (res < 0) {
    ERROR("Error sending to {}:{}", address, port);
    throw rtpmidid::exception("Can not send to address {}:{}. {}", address,
                              port, strerror(errno));
  }

  DEBUG("Sent to {}:{}, {} bytes", address, port, buffer.size());
}

udppeer_t::sockaddr_t *udppeer_t::get_address(const std::string &address,
                                              const std::string &port) {

  auto I = addresses_cache.find(std::pair(address, port));
  if (I != addresses_cache.end()) {
    DEBUG("Cache hit!");
    return &I->second;
  }

  struct addrinfo *sockaddress_list = nullptr;
  struct addrinfo hints {};

  int res = -1;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  const char *cport = port.c_str();
  const char *caddress = address.c_str();

  res = getaddrinfo(caddress, cport, &hints, &sockaddress_list);

  if (res < 0) {
    ERROR("Error resolving address {}:{}", address, port);
    throw rtpmidid::exception("Can not resolve address {}:{}. {}", address,
                              port, strerror(errno));
  }

  if (sockaddress_list == nullptr) {
    ERROR("Error resolving address {}:{}", address, port);
    throw rtpmidid::exception("Can not resolve address {}:{}. {}", address,
                              port, strerror(errno));
  }

  sockaddr addr;
  ::memcpy(&addr, sockaddress_list->ai_addr, sockaddress_list->ai_addrlen);

  if (addresses_cache.size() > MAX_ADDRESS_CACHE_SIZE) {
    addresses_cache.clear();
  }

  auto [J, _] = addresses_cache.emplace(
      std::pair(address, port),
      sockaddr_t{addr, int(sockaddress_list->ai_addrlen)});

  freeaddrinfo(sockaddress_list);
  return &J->second;
}

void udppeer_t::close() {
  listener.stop();
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

} // namespace rtpmidid
