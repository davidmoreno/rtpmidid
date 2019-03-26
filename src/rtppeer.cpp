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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "./logger.hpp"
#include "./rtppeer.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

rtppeer::rtppeer(std::string _name, int startport) : name(std::move(_name)), local_base_port(startport) {
  try {
    remote_base_port = 0; // Not defined
    control_socket = -1;
    midi_socket = -1;

    struct sockaddr_in servaddr;

    control_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_socket < 0){
      throw rtpmidid::exception("Can not open control socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(startport);
    if (bind(control_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open control socket. Maybe addres is in use?");
    }
    if (local_base_port == 0){
      socklen_t len = sizeof(servaddr);
      ::getsockname(control_socket, (struct sockaddr*)&servaddr, &len);
      local_base_port = servaddr.sin_port;
      poller.add_fd_in(control_socket, [this](int){ this->control_data_ready(); });
      DEBUG("Got automatic port {} for control", local_base_port);
    }

    midi_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (midi_socket < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(local_base_port + 1);
    if (bind(midi_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Maybe addres is in use?");
    }
    poller.add_fd_in(midi_socket, [this](int){ this->midi_data_ready(); });

  } catch (...){
    if (control_socket){
      poller.remove_fd(control_socket);
      close(control_socket);
      control_socket = 0;
    }
    if (midi_socket){
      poller.remove_fd(midi_socket);
      close(midi_socket);
      midi_socket = 0;
    }
    throw;
  }
}

rtppeer::~rtppeer(){
  if (control_socket > 0){
    poller.remove_fd(control_socket);
    close(control_socket);
  }
  if (midi_socket > 0){
    poller.remove_fd(midi_socket);
    close(midi_socket);
  }
}

void rtppeer::control_data_ready(){
  uint8_t buffer[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto n = recvfrom(control_socket, buffer, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  DEBUG("Got some data from control: {}", n);
  parse_buffer_t(buffer, buffer + n, buffer + n).print_hex();
}

void rtppeer::midi_data_ready(){
  uint8_t buffer[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto n = recvfrom(midi_socket, buffer, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  DEBUG("Got some data from midi: {}", len);
  parse_buffer_t(buffer, buffer + n, buffer + n).print_hex();
}
