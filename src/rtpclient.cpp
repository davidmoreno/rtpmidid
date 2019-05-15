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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "./rtpclient.hpp"
#include "./netutils.hpp"
#include "./logger.hpp"
#include "./poller.hpp"

using namespace rtpmidid;

rtpclient::rtpclient(std::string name, const std::string &address, int16_t port)
    : peer(std::move(name)) {
  local_base_port = port;
  remote_base_port = -1; // Not defined
  control_socket = -1;
  midi_socket = -1;
  auto startport = local_base_port;
  peer.initiator_id = rand();
  peer.sendto = [this](rtppeer::port_e port, const parse_buffer_t &data){
    this->sendto(port, data);
  };

  connect_to(address, port);
}

rtpclient::~rtpclient(){
  if (peer.is_connected()){
    peer.send_goodbye(rtppeer::CONTROL_PORT);
    peer.send_goodbye(rtppeer::MIDI_PORT);
  }

  if (control_socket > 0){
    poller.remove_fd(control_socket);
    close(control_socket);
  }
  if (midi_socket > 0){
    poller.remove_fd(midi_socket);
    close(midi_socket);
  }
}

void rtpclient::connect_to(std::string address, int port){
  remote_base_port = port;
  try{
    struct sockaddr_in servaddr;
    control_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_socket < 0){
      throw rtpmidid::exception("Can not open control socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);
    if (bind(control_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open control socket. Maybe address is in use?");
    }
    if (local_base_port == 0){
      socklen_t len = sizeof(servaddr);
      ::getsockname(control_socket, (struct sockaddr*)&servaddr, &len);
      local_base_port = htons(servaddr.sin_port);
      DEBUG("Got automatic port {} for control", local_base_port);
    }
    poller.add_fd_in(control_socket, [this](int){ this->data_ready(rtppeer::CONTROL_PORT); });

    midi_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (midi_socket < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(local_base_port + 1);
    if (bind(midi_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Maybe address is in use?");
    }
    poller.add_fd_in(midi_socket, [this](int){ this->data_ready(rtppeer::MIDI_PORT); });
  } catch (...){
    ERROR("Error creating rtp client.");
    if (control_socket){
      poller.remove_fd(control_socket);
      ::close(control_socket);
      control_socket = 0;
    }
    if (midi_socket){
      poller.remove_fd(midi_socket);
      ::close(midi_socket);
      midi_socket = 0;
    }
    throw;
  }

  memset(&peer_addr, 0, sizeof(peer_addr));
  peer_addr.sin_family = AF_INET;
  inet_aton(address.c_str(), &peer_addr.sin_addr);

  DEBUG("Connecting control port {} to {}:{}", local_base_port, address, port);
  peer.connect_to(rtppeer::CONTROL_PORT);
  DEBUG("Connecting midi port {} to {}:{}", local_base_port + 1, address, port + 1);
  peer.connect_to(rtppeer::MIDI_PORT);

  peer.on_connect([this](const std::string &){
    start_ck_1min_sync();
  });
}

void rtpclient::sendto(rtppeer::port_e port, const parse_buffer_t &pb){
  if (port == rtppeer::MIDI_PORT)
    peer_addr.sin_port = htons(remote_base_port + 1);
  else
    peer_addr.sin_port = htons(remote_base_port);

  auto socket = rtppeer::MIDI_PORT == port ? midi_socket : control_socket;

  auto res = ::sendto(
    socket, pb.start, pb.length(),
    MSG_CONFIRM, (const struct sockaddr *)&peer_addr, sizeof(peer_addr)
  );

  if (res != pb.length()){
    throw exception(
      "Could not send all data to {}:{}. Sent {}. {}",
      peer.remote_name, remote_base_port, res, strerror(errno)
    );
  }
}

void rtpclient::reset(){
  remote_base_port = 0;
  peer.reset();
}


void rtpclient::data_ready(rtppeer::port_e port){
  uint8_t raw[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = sizeof(cliaddr);
  auto socket = port == rtppeer::CONTROL_PORT ? control_socket : midi_socket;
  auto n = recvfrom(socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0){
    throw exception("Error reading from rtppeer {}:{}", peer.remote_name, remote_base_port);
  }

  auto buffer = parse_buffer_t(raw, n);
  peer.data_ready(buffer, port);
}

/**
 * Every 60 seconds, do a ck sync.
 *
 * TODO If not answered in some time, asume disconnection.
 */
void rtpclient::start_ck_1min_sync(){
  peer.send_ck0();
  timer_ck = std::move(poller.add_timer_event(60, [this]{
    start_ck_1min_sync();
  }));
}
