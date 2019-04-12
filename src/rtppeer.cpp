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
    initiator_id = 0;

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
    ERROR("Error creating rtppeer.");
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
  unsigned int len = sizeof(cliaddr);
  auto n = recvfrom(control_socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0){
    throw exception("Error reading from rtppeer {}:{}", remote_name, remote_base_port);
  }

  auto buffer = parse_buffer_t(raw, n);

  if (is_command(buffer)){
    if (initiator_id == 0) {
      char *ip = inet_ntoa(cliaddr.sin_addr);
      remote_base_port = htons(cliaddr.sin_port);
      DEBUG("Connected from {}:{}", ip, remote_base_port);
      // First message from other end. Just copy the remote address.
      ::memcpy(&peer_addr, &cliaddr, len);
    }

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
  // DEBUG("Got some data from midi: {}", len);
  auto buffer = parse_buffer_t(raw, n);

  if (is_command(buffer)){
    parse_command(buffer, midi_socket);
  } else {
    parse_midi(buffer);
  }
}


bool is_command(parse_buffer_t &pb){
  // DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 16 && pb.start[0] == 0xFF && pb.start[1] == 0xFF);
}
bool is_feedback(parse_buffer_t &pb){
  // DEBUG("Is feedback? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 12 && pb.start[0] == 0xFF && pb.start[1] == 0xFF && pb.start[2] == 0x52 && pb.start[3] == 0x53);
}

void rtppeer::parse_command(parse_buffer_t &buffer, int socket){
  if (buffer.size() < 16){
    // This should never be reachable, but should help to smart compilers for
    // further size checks
    throw exception("Invalid command packet.");
  }
  // auto _command =
  buffer.read_uint16(); // We already know it is 0xFFFF
  auto command = buffer.read_uint16();
  // DEBUG("Got command type {:X}", command);

  switch(command){
    case rtppeer::OK:
      parse_command_ok(buffer, socket);
      break;
    case rtppeer::IN:
      parse_command_in(buffer, socket);
      break;
    case rtppeer::CK:
      parse_command_ck(buffer, socket);
      break;
    case rtppeer::BY:
      parse_command_by(buffer, socket);
      break;
    default:
      buffer.print_hex(true);
      throw not_implemented();
  }
}

void rtppeer::parse_command_ok(parse_buffer_t &buffer, int _socket){
  auto protocol = buffer.read_uint32();
  auto initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();

  INFO(
    "Got confirmation from {}:{}, initiator_id: {} ({}) ssrc: {}, name: {}",
    remote_name, remote_base_port, initiator_id, this->initiator_id == initiator_id, remote_ssrc, remote_name
  );
}


void rtppeer::parse_command_in(parse_buffer_t &buffer, int socket){
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();

  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }

  INFO(
    "Got connection request from {}:{}, initiator_id: {} ({}) ssrc: {}, name: {}",
    remote_name, remote_base_port, initiator_id, this->initiator_id == initiator_id, remote_ssrc, remote_name
  );

  uint8_t response[128];
  parse_buffer_t response_buffer(response, sizeof(response));
  response_buffer.write_uint16(0xFFFF);
  response_buffer.write_uint16(OK);
  response_buffer.write_uint32(2);
  response_buffer.write_uint32(initiator_id);
  response_buffer.write_uint32(SSRC);
  response_buffer.write_str0(local_name);

  sendto(socket, response_buffer);
}


void rtppeer::parse_command_by(parse_buffer_t &buffer, int socket){
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();

  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }

  if (remote_ssrc != this->remote_ssrc){
    WARNING("Trying to disconnect from the wrong rtpmidi peer (bad port)");
    return;
  }

  INFO("Disconnect from {}:{}", remote_name, remote_base_port);
  // Normally this will schedule removal of peer.
  if (close_peer){
    close_peer();
  }
}

void rtppeer::parse_command_ck(parse_buffer_t &buffer, int socket){
  auto ssrc = buffer.read_uint32();
  auto count = buffer.read_uint8();
  // padding / unused 24 bits
  buffer.read_uint8();
  buffer.read_uint16();

  uint64_t ck1 = buffer.read_uint64();
  uint64_t ck2 = 0;
  uint64_t ck3 = 0;

  switch(count){
    case 0:
    {
      // Send my timestamp. I will use it later when I receive 2.
      ck2 = get_timestamp();
      count = 1;
    }
    break;
    case 1:
    {
      // Send my timestamp. I will use it when get answer with 3.
      ck2 = buffer.read_uint64();
      ck3 = get_timestamp();
      count = 2;
      latency = ck3 - ck1;
      DEBUG("Latency {}: {:.2f} ms", remote_name, latency / 100.0);
    }
    break;
    case 2:
    {
      // Receive the other side CK, I can calculate latency
      ck2 = buffer.read_uint64();
      ck3 = buffer.read_uint64();
      latency = get_timestamp() - ck2;
      DEBUG("Latency {}: {:.2f} ms", remote_name, latency / 100.0);
      // No need to send message
      return;
    }
    default:
      ERROR("Bad CK count. Ignoring.");
      return;
  }

  uint8_t ret[36];
  parse_buffer_t retbuffer(ret, sizeof(ret));
  retbuffer.write_uint16(0xFFFF);
  retbuffer.write_uint16(rtppeer::CK);
  retbuffer.write_uint32(SSRC);
  retbuffer.write_uint8(count);
  // padding
  retbuffer.write_uint8(0);
  retbuffer.write_uint16(0);
  // cks
  retbuffer.write_uint64(ck1);
  retbuffer.write_uint64(ck2);
  retbuffer.write_uint64(ck3);

  // DEBUG("Got packet CK");
  // buffer.print_hex(true);
  //
  // DEBUG("Send packet CK");
  // retbuffer.print_hex();

  sendto(socket, retbuffer);
}

void rtppeer::parse_feedback(parse_buffer_t &buffer){
  buffer.position = buffer.start + 8;
  seq_nr_ack = buffer.read_uint16();

  DEBUG("Got feedback until package {} / {}. No journal, so ignoring.", seq_nr_ack, seq_nr);
}

void rtppeer::parse_midi(parse_buffer_t &buffer){
  auto _headers = buffer.read_uint8();
  auto rtpmidi_id = buffer.read_uint8();
  if (rtpmidi_id != 0x61){
    WARNING("Received packet which is not RTP MIDI. Ignoring.");
    return;
  }
  remote_seq_nr = buffer.read_uint16();
  // In the future we may use a journal.
  auto _remote_timestamp = buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();
  if (remote_ssrc != this->remote_ssrc){
    WARNING("Got message for unknown remote SSRC on this port. (from {:04X}, I'm {:04X})", remote_ssrc, this->remote_ssrc);
    return;
  }


  auto header = buffer.read_uint8();
  if ((header & 0xF0) != 0){
    WARNING("This RTP MIDI MIDI header is too complex. Not implemented yet. Ignoring.");
    return;
  }
  int16_t length = header & 0x0F;
  buffer.check_enought(length);

  parse_buffer_t midi_data(buffer.position, length);
  emit_midi_events(midi_data);
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

  sendto(midi_socket, buffer);
}

void rtppeer::send_goodbye(int from_fd, int to_port){
  uint8_t data[64];
  parse_buffer_t buffer(data, sizeof(data));

  buffer.write_uint16(0x0FFFF);
  buffer.write_uint16(::rtppeer::BY);
  buffer.write_uint32(2);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(SSRC);

  sendto(from_fd, buffer);
}

void rtppeer::sendto(int socket, const parse_buffer_t &pb){
  if (socket == midi_socket)
    peer_addr.sin_port = htons(remote_base_port + 1);
  else
    peer_addr.sin_port = htons(remote_base_port);

  auto res = ::sendto(
    socket, pb.start, pb.length(),
    MSG_CONFIRM, (const struct sockaddr *)&peer_addr, sizeof(peer_addr)
  );

  if (res != pb.length()){
    throw exception("Could not send all data to {}:{}. Sent {}", remote_name, remote_base_port, res);
  }
}
