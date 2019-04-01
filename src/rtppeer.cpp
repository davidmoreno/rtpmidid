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
#include <time.h>

#include "./logger.hpp"
#include "./rtppeer.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

bool is_command(parse_buffer_t &);
bool is_feedback(parse_buffer_t &);

/**
 * @short Generic peer constructor
 *
 * A generic peer can be a client or a server. In any case it has a control
 * and midi ports. The port can be random for clients, or fixed for server.
 *
 * BUGS: It needs two consecutive ports for client, but just ask a random and
 *       expects next to be free. It almost always is, but can fail.
 */
rtppeer::rtppeer(std::string _name, int startport) : local_base_port(startport), local_name(std::move(_name)) {
  try {
    remote_base_port = 0; // Not defined
    control_socket = -1;
    midi_socket = -1;
    remote_ssrc = 0;
    seq_nr = rand() & 0x0FFFF;
    seq_nr_ack = seq_nr;
    timestamp_start = 0;
    timestamp_start = get_timestamp();

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
      local_base_port = htons(servaddr.sin_port);
      DEBUG("Got automatic port {} for control", local_base_port);
    }
    poller.add_fd_in(control_socket, [this](int){ this->control_data_ready(); });

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
  DEBUG("~rtppeer {}", local_name);

  send_goodbye(control_socket, remote_base_port);
  send_goodbye(midi_socket, remote_base_port+1);
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
  uint8_t raw[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto n = recvfrom(control_socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  DEBUG("Got some data from control: {}", n);
  auto buffer = parse_buffer_t(raw, n);

  if (is_command(buffer)){
    parse_command(buffer, control_socket);
  } else if (is_feedback(buffer)) {
    parse_feedback(buffer);
  } else {
    buffer.print_hex(true);
  }

}

void rtppeer::midi_data_ready(){
  uint8_t raw[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = 0;
  auto n = recvfrom(midi_socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  DEBUG("Got some data from midi: {}", len);
  auto buffer = parse_buffer_t(raw, n);

  if (is_command(buffer)){
    parse_command(buffer, control_socket);
  } else {
    buffer.print_hex(true);
  }
}


bool is_command(parse_buffer_t &pb){
  DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 16 && pb.start[0] == 0xFF && pb.start[1] == 0xFF);
}
bool is_feedback(parse_buffer_t &pb){
  DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 12 && pb.start[0] == 0xFF && pb.start[1] == 0xFF && pb.start[2] == 0x52 && pb.start[3] == 0x53);
}

void rtppeer::parse_command(parse_buffer_t &buffer, int port){
  if (buffer.size() < 16){
    // This should never be reachable, but should help to smart compilers for
    // further size checks
    throw exception("Invalid command packet.");
  }
  // auto _command =
  buffer.read_uint16(); // We already know it is 0xFFFF
  auto command = buffer.read_uint16();
  DEBUG("Got command type {:X}", command);

  switch(command){
    case rtppeer::OK:
      parse_command_ok(buffer, port);
      break;
    default:
      throw not_implemented();
  }
}

void rtppeer::parse_command_ok(parse_buffer_t &buffer, int port){
  auto protocol = buffer.read_uint32();
  auto initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();

  INFO(
    "Got confirmation from {}:{}, initiator_id: {} ({}) ssrc: {}, name: {}",
    remote_name, remote_base_port, initiator_id, this->initiator_id == initiator_id, remote_ssrc, remote_name
  );
}

void rtppeer::parse_feedback(parse_buffer_t &buffer){
  buffer.position = buffer.start + 8;
  seq_nr_ack = buffer.read_uint16();

  DEBUG("Got feedback until package {} / {}. No journal, so ignoring.", seq_nr_ack, seq_nr);
}

uint64_t rtppeer::get_timestamp(){
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);
  uint64_t now = spec.tv_sec * 1000 + spec.tv_nsec / 1.0e7;

  return uint32_t(now - timestamp_start);
}

void rtppeer::send_midi(parse_buffer_t *events){
  uint8_t data[512];
  parse_buffer_t buffer(data, sizeof(data));

  uint32_t timestamp = get_timestamp();
  seq_nr++;

  buffer.write_uint8(0x80);
  buffer.write_uint8(0x61);
  buffer.write_uint16(seq_nr);
  buffer.write_uint32(timestamp);
  buffer.write_uint32(SSRC);

  // Now midi
  buffer.write_uint8(events->size());
  buffer.copy_from(*events);

  // buffer.print_hex();

  peer_addr.sin_port = htons(remote_base_port+1);
  sendto(midi_socket, buffer.start, buffer.length(), MSG_CONFIRM, (const struct sockaddr *)&peer_addr, sizeof(peer_addr));
}

void rtppeer::send_goodbye(int from_fd, int to_port){
  uint8_t data[64];
  parse_buffer_t buffer(data, sizeof(data));

  buffer.write_uint16(0x0FFFF);
  buffer.write_uint16(::rtppeer::BY);
  buffer.write_uint32(2);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(SSRC);

  peer_addr.sin_port = htons(to_port);
  sendto(from_fd, buffer.start, buffer.length(), MSG_CONFIRM, (const struct sockaddr *)&peer_addr, sizeof(peer_addr));
}
