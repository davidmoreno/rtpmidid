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
#include "rtpmidid/exceptions.hpp"
#include "rtpmidid/network.hpp"
#include "rtpmidid/networkaddress.hpp"
#include "rtpmidid/poller.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEBUG0(...) // do nothing

namespace rtpmidid {

udppeer_t::udppeer_t(const network_address_t &addr) { open(addr); }

int udppeer_t::open(const network_address_t &address) {
  auto addr = address.get_sockaddr();
  auto addrlen = address.get_socklen();

  DEBUG0("Opening UDP port at {}", address.to_string());

  fd = socket(address.get_aifamily(), SOCK_DGRAM | SOCK_CLOEXEC, 0);

  if (fd < 0) {
    ERROR("Error creating socket: {}", strerror(errno));
    return -1; // Bad socket, try next
  }
  auto ret = bind(fd, addr, addrlen);
  if (ret != 0) {
    ERROR("Error binding socket: {} {}", address.to_string(), strerror(errno));
    ::close(fd);
    fd = -1;
    return -1;
  }

  network_address_t myaddress{fd};

  DEBUG0("UDP port listen ready, at {}, fd {}", myaddress.to_string(), fd);

  if (!listener) {
    listener = poller.add_fd_in(fd, [this](int fd) {
      assert(fd == this->fd); // Should be the same.
      data_ready();
    });
  }

  return fd;
}

int udppeer_t::open(const network_address_list_t &address_list) {
  if (fd >= 0) {
    close();
  }
  for (auto &address : address_list) {
    if (open(address) >= 0) {
      return fd;
    }
  }
  ERROR("Could not open any address from list");
  return -1;
}

void udppeer_t::data_ready() {
  std::array<uint8_t, 1500> raw{};
  struct sockaddr_storage cliaddr {};
  unsigned int len = sizeof(cliaddr);
  auto n = recvfrom(fd, raw.data(), 1500, MSG_DONTWAIT,
                    sockaddr_storage_to_sockaddr(&cliaddr), &len);
  // DEBUG0("Got some data from control: {}", n);

  network_address_t network_address{&cliaddr, len};

  if (n < 0) {
    throw exception("Error reading at {}", network_address.to_string());
  }

  packet_t packet(raw.data(), n);

  // DEBUG0("Got data from {}, {} bytes", network_address.to_string(), n);

  on_read(packet, network_address);
}

ssize_t udppeer_t::sendto(const packet_t &packet,
                          const network_address_t &addr) {

  auto res = ::sendto(fd, packet.get_data(), packet.get_size(), 0,
                      addr.get_sockaddr(), addr.get_socklen());

  if (res < 0) {
    std::string addr_str = addr.to_string();
    ERROR("Error sending to {}. This is UDP... so just lost! ({})", addr_str,
          strerror(errno));
    // throw rtpmidid::exception("Can not send to address {}. {}", addr_str,
    //                           strerror(errno));
  }

  // DEBUG0("Sent to {} bytes", packet);
  return res;
}

network_address_t udppeer_t::get_address() {
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (fd < 0) {
    return network_address_t();
  }

  if (getsockname(fd, sockaddr_storage_to_sockaddr(&addr), &len) < 0) {
    throw rtpmidid::exception("Error getting address {}", strerror(errno));
  }
  return network_address_t{&addr, len}.dup();
}

void udppeer_t::close() {
  listener.stop();
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

} // namespace rtpmidid
