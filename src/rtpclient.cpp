/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <fmt/format.h>

#include "./rtpclient.hpp"
#include "./netutils.hpp"
#include "./logger.hpp"
#include "./poller.hpp"

using namespace rtpmidid;

// A random int32. Should be configurable, so diferent systems, have diferent SSRC.
const uint32_t SSRC = 0x111f6c31;

rtpclient::rtpclient(std::string name, const uint8_t *ip4, int16_t port) : rtppeer(std::move(name), 0){
  remote_base_port = port;
  initiator_id = rand();

  memset(&peer_addr, 0, sizeof(peer_addr));
  peer_addr.sin_family = AF_INET;
  // FIXME. For sure there is a direct path from four bytes to ip addr.
  auto dottedaddr = fmt::format("{}.{}.{}.{}", uint8_t(ip4[0]), uint8_t(ip4[1]), uint8_t(ip4[2]), uint8_t(ip4[3]));
  inet_aton(dottedaddr.c_str(), &peer_addr.sin_addr);

  DEBUG("Connecting control port {} to {}:{}", local_base_port, dottedaddr, port);
  connect_to(control_socket, port);
  DEBUG("Connecting midi port {} to {}:{}", local_base_port + 1, dottedaddr, port + 1);
  connect_to(midi_socket, port + 1);
}

bool rtpclient::connect_to(int socketfd, int16_t port){
  peer_addr.sin_port = htons(port);

  uint8_t packet[1500];
  auto buffer = parse_buffer_t(packet, 1500);


  auto signature = 0xFFFF;
  auto command = rtppeer::IN;
  auto protocol = 2;
  auto sender = SSRC;

  buffer.write_uint16(signature);
  buffer.write_uint16(command);
  buffer.write_uint32(protocol);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(sender);
  buffer.write_str0(name);

  DEBUG("Send packet:");
  buffer.print_hex();

  sendto(socketfd, packet, buffer.length(), MSG_CONFIRM, (const struct sockaddr *)&peer_addr, sizeof(peer_addr));

  return true;
}
