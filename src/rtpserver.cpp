/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
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
#include <unistd.h>

#include "./rtpserver.hpp"
#include "./poller.hpp"
#include "./exceptions.hpp"
#include "./logger.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

rtpserver::rtpserver(std::string name, int16_t port) : peer(std::move(name)){
  INFO("Listening RTP MIDI connections at 0.0.0.0:{}, with name: '{}'",
    port, name);
  control_socket = midi_socket = 0;
  control_port = port;
  midi_port = port + 1;

  try{
    struct sockaddr_in servaddr;
    control_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_socket < 0){
      throw rtpmidid::exception("Can not open control socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(control_port);
    if (bind(control_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open control socket. Maybe address is in use?");
    }
    if (control_socket == 0){
      socklen_t len = sizeof(servaddr);
      ::getsockname(control_socket, (struct sockaddr*)&servaddr, &len);
      control_port = htons(servaddr.sin_port);
      DEBUG("Got automatic port {} for control", control_port);
      midi_port = control_port + 1;
    }

    poller.add_fd_in(control_socket, [this](int){ this->data_ready(rtppeer::CONTROL_PORT); });

    midi_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (midi_socket < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(midi_port);
    if (bind(midi_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Maybe address is in use?");
    }
    poller.add_fd_in(midi_socket, [this](int){ this->data_ready(rtppeer::MIDI_PORT); });
  } catch (...){
    ERROR("Error creating server.");
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
}

rtpserver::~rtpserver(){
  if (control_socket > 0){
    poller.remove_fd(control_socket);
    close(control_socket);
  }
  if (midi_socket > 0){
    poller.remove_fd(midi_socket);
    close(midi_socket);
  }
}

void rtpserver::data_ready(rtppeer::port_e port){
  DEBUG("Data ready at server");

  uint8_t raw[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = sizeof(cliaddr);
  auto socket = port == rtppeer::CONTROL_PORT ? control_socket : midi_socket;
  auto n = recvfrom(socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0){
    auto netport = port == rtppeer::CONTROL_PORT ? control_port : midi_port;
    throw exception("Error reading from rtppeer {}:{}", peer.remote_name, netport);
  }

  auto buffer = parse_buffer_t(raw, n);
  peer.data_ready(buffer, port);
}
