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

rtpclient::rtpclient(std::string name, const std::string &address, int16_t port) : rtppeer(std::move(name), 0){
  remote_base_port = port;
  initiator_id = rand();

  memset(&peer_addr, 0, sizeof(peer_addr));
  peer_addr.sin_family = AF_INET;
  // FIXME. For sure there is a direct path from four bytes to ip addr.
  inet_aton(address.c_str(), &peer_addr.sin_addr);

  DEBUG("Connecting control port {} to {}:{}", local_base_port, address, port);
  connect_to(control_socket, port);
  DEBUG("Connecting midi port {} to {}:{}", local_base_port + 1, address, port + 1);
  connect_to(midi_socket, port + 1);

  on_connect([this](const std::string &){
    start_ck_1min_sync();
  });
}

rtpclient::~rtpclient(){

}


/**
 * Every 60 seconds, do a ck sync.
 *
 * TODO If not answered in some time, asume disconnection.
 */
void rtpclient::start_ck_1min_sync(){
  send_ck0();
  timer_ck = std::move(poller.add_timer_event(60, [this]{
    start_ck_1min_sync();
  }));
}

bool rtpclient::connect_to(int socketfd, int16_t port){
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
  buffer.write_str0(local_name);

  // DEBUG("Send packet:");
  // buffer.print_hex();

  peer_addr.sin_port = htons(port);
  sendto(socketfd, buffer);

  return true;
}
