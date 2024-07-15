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
#include "rtpmidid/networkaddress.hpp"
#include "rtpmidid/poller.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace rtpmidid {

const auto MAX_ADDRESS_CACHE_SIZE = 100;

int udppeer_t::open(const std::string &address, const std::string &port) {

  network_address_t::resolve_loop(address, port, [this](const addrinfo *addr) {
    fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (fd < 0) {
      return false; // Bad socket, try next
    }
    auto ret = bind(fd, addr->ai_addr, addr->ai_addrlen);
    if (ret != 0) {
      ::close(fd);
      fd = -1;
      return false;
    }
    return true; // Success
  });

  network_address_t myaddress{fd};

  DEBUG("UDP port listen ready, at {}, fd {}", myaddress.to_string(), fd);

  listener = poller.add_fd_in(fd, [this](int fd) {
    assert(fd == this->fd); // Should be the same.
    data_ready();
  });

  return fd;
}

void udppeer_t::data_ready() {
  std::array<uint8_t, 1500> raw{};
  struct sockaddr_storage cliaddr {};
  unsigned int len = sizeof(cliaddr);
  auto n = recvfrom(fd, raw.data(), 1500, MSG_DONTWAIT,
                    sockaddr_storage_to_sockaddr(&cliaddr), &len);
  // DEBUG("Got some data from control: {}", n);

  network_address_t network_address{&cliaddr, len};

  if (n < 0) {
    throw exception("Error reading at {}", network_address.to_string());
  }

  auto buffer = io_bytes_reader(raw.data(), n);

  DEBUG("Got data from {}, {} bytes", network_address.to_string(), n);

  on_read(buffer, network_address);
}

void udppeer_t::send(io_bytes &buffer, const std::string &address,
                     const std::string &port) {

  auto addr = get_address(address, port);

  auto res = sendto(fd, buffer.start, buffer.size(), 0, addr->get_sockaddr(),
                    addr->get_socklen());

  if (res < 0) {
    ERROR("Error sending to {}:{}", address, port);
    throw rtpmidid::exception("Can not send to address {}:{}. {}", address,
                              port, strerror(errno));
  }

  DEBUG("Sent to {}:{}, {} bytes", address, port, buffer.size());
}

network_address_t const *udppeer_t::get_address(const std::string &address,
                                                const std::string &port) {

  auto I = addresses_cache.find(std::pair(address, port));
  if (I != addresses_cache.end()) {
    DEBUG("Cache hit!");
    return &I->second;
  }

  auto resolved = network_address_t::resolve_loop(
      address, port, [this, &address, &port](const network_address_t &addr) {
        addresses_cache.emplace(std::pair(address, port), addr.dup());
        return true;
      });

  if (!resolved) {
    ERROR("Error resolving address {}:{}", address, port);
    return nullptr;
  }

  I = addresses_cache.find(std::pair(address, port));
  if (I == addresses_cache.end()) {
    ERROR("Error getting address {}:{} from cache", address, port);
    return nullptr;
  }
  return &I->second;
}

void udppeer_t::close() {
  listener.stop();
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

} // namespace rtpmidid
