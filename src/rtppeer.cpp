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
#include <time.h>

#include "./logger.hpp"
#include "./rtppeer.hpp"
#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

/**
 * @short Generic peer constructor
 *
 * A generic peer can be a client or a server. In any case it has a control
 * and midi ports. The port can be random for clients, or fixed for server.
 *
 * BUGS: It needs two consecutive ports for client, but just ask a random and
 *       expects next to be free. It almost always is, but can fail.
 */
rtppeer::rtppeer(std::string _name) : local_name(std::move(_name)) {
  status = NOT_CONNECTED;
  remote_ssrc = 0;
  local_ssrc = rand() & 0x0FFFF;
  seq_nr = rand() & 0x0FFFF;
  seq_nr_ack = seq_nr;
  timestamp_start = 0;
  timestamp_start = get_timestamp();
  initiator_id = 0;
}

rtppeer::~rtppeer(){
  DEBUG("~rtppeer '{}' (local) <-> '{}' (remote)", local_name, remote_name);
}

void rtppeer::reset(){
  status = NOT_CONNECTED;
  remote_name = "";
  remote_ssrc = 0;
  initiator_id = 0;
}

void rtppeer::data_ready(parse_buffer_t &buffer, port_e port){
  if (port == CONTROL_PORT){
    if (is_command(buffer)){
      parse_command(buffer, port);
    } else if (is_feedback(buffer)) {
      parse_feedback(buffer);
    } else {
      buffer.print_hex(true);
    }
  } else {
    if (is_command(buffer)){
      parse_command(buffer, port);
    } else {
      parse_midi(buffer);
    }
  }
}

bool rtppeer::is_command(parse_buffer_t &pb){
  // DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 16 && pb.start[0] == 0xFF && pb.start[1] == 0xFF);
}
bool rtppeer::is_feedback(parse_buffer_t &pb){
  // DEBUG("Is feedback? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF, pb.start[1] == 0xFF);
  return (pb.size() >= 12 && pb.start[0] == 0xFF && pb.start[1] == 0xFF && pb.start[2] == 0x52 && pb.start[3] == 0x53);
}

void rtppeer::parse_command(parse_buffer_t &buffer, port_e port){
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
      parse_command_ok(buffer, port);
      break;
    case rtppeer::IN:
      parse_command_in(buffer, port);
      break;
    case rtppeer::CK:
      parse_command_ck(buffer, port);
      break;
    case rtppeer::BY:
      parse_command_by(buffer, port);
      break;
    case rtppeer::NO:
      parse_command_no(buffer, port);
      break;
    default:
      buffer.print_hex(true);
      throw not_implemented();
  }
}

/**
 * This command is received when I am a client.
 *
 * I already sent before the IN message, the server sent me OK
 */
void rtppeer::parse_command_ok(parse_buffer_t &buffer, port_e port){
  if (status == CONNECTED){
    WARNING("This peer is already connected. Need to disconnect to connect again.");
    return;
  }
  auto protocol = buffer.read_uint32();
  auto initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();
  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }
  if (initiator_id != this->initiator_id){
    throw exception("Response to connect from an unknown initiator. Not connecting.");
  }

  INFO(
    "Got confirmation from {}, initiator_id: {} ({}) ssrc: {}, name: {}, port: {}",
    remote_name, initiator_id, this->initiator_id == initiator_id, remote_ssrc, remote_name,
    port == CONTROL_PORT ? "Control" : port == MIDI_PORT ? "MIDI" : "Unknown"
  );
  if (port == MIDI_PORT) {
    status = status_e(int(status) | int(MIDI_CONNECTED));
  } else if (port == CONTROL_PORT){
    status = status_e(int(status) | int(CONTROL_CONNECTED));
  } else {
    ERROR("Got data on unknown PORT! {}", port);
  }
  connected_event(remote_name, status);
}


/**
 * This command is received when I am a server.
 *
 * A client sent me IN, I answer with OK, and we are set.
 *
 * TODO It would be super nice to be able to have several clients
 * connected to me.
 */
void rtppeer::parse_command_in(parse_buffer_t &buffer, port_e port){
  if (status == CONNECTED){
    WARNING("This peer is already connected. Need to disconnect to connect again.");
    return;
  }
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();

  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }

  INFO(
    "Got connection request from {}, initiator_id: {:X} ({}) ssrc: {:X}, name: {}, at control? {}",
    remote_name, initiator_id, this->initiator_id == initiator_id, remote_ssrc, remote_name, port == CONTROL_PORT
  );

  uint8_t response[128];
  parse_buffer_t response_buffer(response, sizeof(response));
  response_buffer.write_uint16(0xFFFF);
  response_buffer.write_uint16(OK);
  response_buffer.write_uint32(2);
  response_buffer.write_uint32(initiator_id);
  response_buffer.write_uint32(local_ssrc);
  response_buffer.write_str0(local_name);

  send_event(std::move(response_buffer), port);

  if (port == MIDI_PORT)
    status = status_e(int(status) | int(MIDI_CONNECTED));
  if (port == CONTROL_PORT)
    status = status_e(int(status) | int(CONTROL_CONNECTED));

  connected_event(remote_name, status);
}


void rtppeer::parse_command_by(parse_buffer_t &buffer, port_e port){
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();

  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }

  if (remote_ssrc != this->remote_ssrc){
    WARNING("Trying to disconnect from the wrong rtpmidi peer (bad port)");
    return;
  }

  status = (status_e) (((int)status) & ~((int)(port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED)));
  INFO("Disconnect from {}, {} port. Status {:X}", remote_name, port == MIDI_PORT ? "MIDI" : "Control", (int)status);

  disconnect_event(PEER_DISCONNECTED);
}

void rtppeer::parse_command_no(parse_buffer_t &buffer, port_e port){
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();

  if (protocol != 2){
    throw exception("rtpmidid only understands RTP MIDI protocol 2. Fill an issue at https://github.com/davidmoreno/rtpmidid/. Got protocol {}", protocol);
  }

  status = (status_e) (((int)status) & ~((int)(port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED)));
  WARNING("Invitation Rejected (NO) : remote ssrc {:X}",remote_ssrc);
  INFO("Disconnect from {}, {} port. Status {:X}", remote_name, port == MIDI_PORT ? "MIDI" : "Control", (int)status);

  disconnect_event(CONNECTION_REJECTED);
}

void rtppeer::parse_command_ck(parse_buffer_t &buffer, port_e port){
  // auto ssrc =
  buffer.read_uint32();
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
      DEBUG("Latency {}: {:.2f} ms (client / 2)", remote_name, latency / 10.0);
    }
    break;
    case 2:
    {
      // Receive the other side CK, I can calculate latency
      ck2 = buffer.read_uint64();
      // ck3 = buffer.read_uint64();
      latency = get_timestamp() - ck2;
      DEBUG("Latency {}: {:.2f} ms (server / 3)", remote_name, latency / 10.0);
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
  retbuffer.write_uint32(local_ssrc);
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

  send_event(std::move(retbuffer), port);
}

void rtppeer::send_ck0(){
  uint64_t ck1 = get_timestamp();
  uint64_t ck2 = 0;
  uint64_t ck3 = 0;

  uint8_t ret[36];
  parse_buffer_t retbuffer(ret, sizeof(ret));
  retbuffer.write_uint16(0xFFFF);
  retbuffer.write_uint16(rtppeer::CK);
  retbuffer.write_uint32(local_ssrc);
  retbuffer.write_uint8(0);
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

  send_event(std::move(retbuffer), MIDI_PORT);
}

void rtppeer::parse_feedback(parse_buffer_t &buffer){
  buffer.position = buffer.start + 8;
  seq_nr_ack = buffer.read_uint16();

  DEBUG("Got feedback until package {} / {}. No journal, so ignoring.", seq_nr_ack, seq_nr);
}

void rtppeer::parse_midi(parse_buffer_t &buffer){
  // auto _headers =
  buffer.read_uint8();
  auto rtpmidi_id = buffer.read_uint8();
  if (rtpmidi_id != 0x61){
    WARNING("Received packet which is not RTP MIDI. Ignoring.");
    return;
  }
  remote_seq_nr = buffer.read_uint16();
  // TODO In the future we may use a journal.
  // auto _remote_timestamp =
  buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();
  if (remote_ssrc != this->remote_ssrc){
    WARNING("Got message for unknown remote SSRC on this port. (from {:04X}, I'm {:04X})", remote_ssrc, this->remote_ssrc);
    return;
  }


  auto header = buffer.read_uint8();
  if ((header & 0x80) != 0){
    WARNING("This RTP MIDI header has journal. Not implemented yet. Ignoring.");
  }
  if ((header & 0xB0) != 0){
    WARNING("This RTP MIDI header is too complex. Not implemented yet. Ignoring.");
    return;
  }
  int16_t length = header & 0x0F;
  buffer.check_enought(length);

  parse_buffer_t midi_data(buffer.position, length);

  midi_event(midi_data);
}

/**
 * Returns the times since start of this object in 100 us (1e-4) / 0.1 ms
 *
 * 10 ts = 1ms, 10000 ts = 1s. 1ms = 0.1ts
 */
uint64_t rtppeer::get_timestamp(){
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);
  uint64_t now = spec.tv_sec * 1000 + spec.tv_nsec / 1.0e7;

  return uint32_t(now - timestamp_start);
}

void rtppeer::send_midi(parse_buffer_t &events){
  if (!is_connected()){ // Not connected yet.
    DEBUG("Can not send MIDI data to {} yet, not connected ({:X}).", remote_name, (int)status);
    return;
  }

  uint8_t data[512];
  parse_buffer_t buffer(data, sizeof(data));

  uint32_t timestamp = get_timestamp();
  seq_nr++;

  buffer.write_uint8(0x80);
  buffer.write_uint8(0x61);
  buffer.write_uint16(seq_nr);
  buffer.write_uint32(timestamp);
  buffer.write_uint32(local_ssrc);

  // Now midi
  buffer.write_uint8(events.size());
  buffer.copy_from(events);

  // events.print_hex();
  // buffer.print_hex();

  send_event(std::move(buffer), MIDI_PORT);
}

void rtppeer::send_goodbye(port_e to_port){
  uint8_t data[64];
  parse_buffer_t buffer(data, sizeof(data));

  buffer.write_uint16(0x0FFFF);
  buffer.write_uint16(::rtppeer::BY);
  buffer.write_uint32(2);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(local_ssrc);

  send_event(std::move(buffer), to_port);

  status = status_e(int(status) & ~int(to_port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED));

  if (status == NOT_CONNECTED){
    disconnect_event(DISCONNECT);
  }
}

void rtppeer::connect_to(port_e rtp_port){
  uint8_t packet[1500];
  auto buffer = parse_buffer_t(packet, 1500);

  auto signature = 0xFFFF;
  auto command = rtppeer::IN;
  auto protocol = 2;
  auto sender = local_ssrc;

  buffer.write_uint16(signature);
  buffer.write_uint16(command);
  buffer.write_uint32(protocol);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(sender);
  buffer.write_str0(local_name);

  // DEBUG("Send packet:");
  // buffer.print_hex();

  send_event(std::move(buffer), rtp_port);
}
