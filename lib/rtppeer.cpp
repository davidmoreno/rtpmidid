/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/rtppeer.hpp>

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
  remote_seq_nr = 0; // Just not radom memory data
  timestamp_start = 0;
  timestamp_start = get_timestamp();
  initiator_id = 0;
  waiting_ck = false;
}

rtppeer::~rtppeer() {
  DEBUG("~rtppeer '{}' (local) <-> '{}' (remote)", local_name, remote_name);
}

void rtppeer::reset() {
  status = NOT_CONNECTED;
  remote_name = "";
  remote_ssrc = 0;
  initiator_id = 0;
}

void rtppeer::data_ready(io_bytes_reader &&buffer, port_e port) {
  if (port == CONTROL_PORT) {
    if (is_command(buffer)) {
      parse_command(buffer, port);
    } else {
      buffer.print_hex(true);
    }
  } else {
    if (is_command(buffer)) {
      parse_command(buffer, port);
    } else {
      parse_midi(buffer);
    }
  }
}

bool rtppeer::is_command(io_bytes_reader &pb) {
  // DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF,
  // pb.start[1] == 0xFF);
  return (pb.size() >= 16 && pb.start[0] == 0xFF && pb.start[1] == 0xFF);
}

void rtppeer::parse_command(io_bytes_reader &buffer, port_e port) {
  if (buffer.size() < 16) {
    // This should never be reachable, but should help to smart compilers for
    // further size checks
    throw exception("Invalid command packet.");
  }
  // auto _command =
  buffer.read_uint16(); // We already know it is 0xFFFF
  auto command = buffer.read_uint16();
  // DEBUG("Got command type {:X}", command);

  switch (command) {
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
  case rtppeer::RS:
    parse_command_rs(buffer, port);
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
void rtppeer::parse_command_ok(io_bytes_reader &buffer, port_e port) {
  if (status == CONNECTED) {
    WARNING(
        "This peer is already connected. Need to disconnect to connect again.");
    return;
  }
  auto protocol = buffer.read_uint32();
  auto initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();
  if (protocol != 2) {
    throw exception(
        "rtpmidid only understands RTP MIDI protocol 2. Fill an issue at "
        "https://github.com/davidmoreno/rtpmidid/. Got protocol {}",
        protocol);
  }
  if (initiator_id != this->initiator_id) {
    throw exception(
        "Response to connect from an unknown initiator. Not connecting.");
  }

  INFO("Got confirmation from {}, initiator_id: {} ({}) ssrc: {}, name: {}, "
       "port: {}",
       remote_name, initiator_id, this->initiator_id == initiator_id,
       remote_ssrc, remote_name,
       port == CONTROL_PORT ? "Control"
                            : port == MIDI_PORT ? "MIDI" : "Unknown");
  if (port == MIDI_PORT) {
    status = status_e(int(status) | int(MIDI_CONNECTED));
  } else if (port == CONTROL_PORT) {
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
void rtppeer::parse_command_in(io_bytes_reader &buffer, port_e port) {
  if (status == CONNECTED) {
    WARNING(
        "This peer is already connected. Need to disconnect to connect again.");
    return;
  }
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  remote_ssrc = buffer.read_uint32();
  remote_name = buffer.read_str0();

  if (protocol != 2) {
    throw exception(
        "rtpmidid only understands RTP MIDI protocol 2. Fill an issue at "
        "https://github.com/davidmoreno/rtpmidid/. Got protocol {}",
        protocol);
  }

  INFO("Got connection request from {}, initiator_id: {:X} ({}) ssrc: {:X}, "
       "name: {}, at control? {}",
       remote_name, initiator_id, this->initiator_id == initiator_id,
       remote_ssrc, remote_name, port == CONTROL_PORT);

  io_bytes_writer_static<128> response;
  response.write_uint16(0xFFFF);
  response.write_uint16(OK);
  response.write_uint32(2);
  response.write_uint32(initiator_id);
  response.write_uint32(local_ssrc);
  response.write_str0(local_name);

  send_event(response, port);

  if (port == MIDI_PORT)
    status = status_e(int(status) | int(MIDI_CONNECTED));
  if (port == CONTROL_PORT)
    status = status_e(int(status) | int(CONTROL_CONNECTED));

  connected_event(remote_name, status);
}

void rtppeer::parse_command_by(io_bytes_reader &buffer, port_e port) {
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();

  if (protocol != 2) {
    throw exception(
        "rtpmidid only understands RTP MIDI protocol 2. Fill an issue at "
        "https://github.com/davidmoreno/rtpmidid/. Got protocol {}",
        protocol);
  }

  if (remote_ssrc != this->remote_ssrc) {
    WARNING("Trying to disconnect from the wrong rtpmidi peer (bad port)");
    return;
  }

  status = (status_e)(
      ((int)status) &
      ~((int)(port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED)));
  INFO("Disconnect from {}, {} port. Status {:X}", remote_name,
       port == MIDI_PORT ? "MIDI" : "Control", (int)status);

  disconnect_event(PEER_DISCONNECTED);
}

void rtppeer::parse_command_no(io_bytes_reader &buffer, port_e port) {
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  auto remote_ssrc = buffer.read_uint32();

  if (protocol != 2) {
    throw exception(
        "rtpmidid only understands RTP MIDI protocol 2. Fill an issue at "
        "https://github.com/davidmoreno/rtpmidid/. Got protocol {}",
        protocol);
  }

  status = (status_e)(
      ((int)status) &
      ~((int)(port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED)));
  WARNING("Invitation Rejected (NO) : remote ssrc {:X}", remote_ssrc);
  INFO("Disconnect from {}, {} port. Status {:X}", remote_name,
       port == MIDI_PORT ? "MIDI" : "Control", (int)status);

  disconnect_event(CONNECTION_REJECTED);
}

void rtppeer::parse_command_ck(io_bytes_reader &buffer, port_e port) {
  // auto ssrc =
  buffer.read_uint32();
  auto count = buffer.read_uint8();
  // padding / unused 24 bits
  buffer.read_uint8();
  buffer.read_uint16();

  uint64_t ck1 = buffer.read_uint64();
  uint64_t ck2 = 0;
  uint64_t ck3 = 0;

  switch (count) {
  case 0: {
    // Send my timestamp. I will use it later when I receive 2.
    ck2 = get_timestamp();
    count = 1;
  } break;
  case 1: {
    // Send my timestamp. I will use it when get answer with 3.
    ck2 = buffer.read_uint64();
    ck3 = get_timestamp();
    count = 2;
    latency = ck3 - ck1;
    waiting_ck = false;
    INFO("Latency {}: {:.2f} ms (client / 2)", remote_name, latency / 10.0);
    ck_event(latency / 10.0);
  } break;
  case 2: {
    // Receive the other side CK, I can calculate latency
    ck2 = buffer.read_uint64();
    // ck3 = buffer.read_uint64();
    latency = get_timestamp() - ck2;
    INFO("Latency {}: {:.2f} ms (server / 3)", remote_name, latency / 10.0);
    // No need to send message
    ck_event(latency / 10.0);
    return;
  }
  default:
    ERROR("Bad CK count. Ignoring.");
    return;
  }

  io_bytes_writer_static<36> response;
  response.write_uint16(0xFFFF);
  response.write_uint16(rtppeer::CK);
  response.write_uint32(local_ssrc);
  response.write_uint8(count);
  // padding
  response.write_uint8(0);
  response.write_uint16(0);
  // cks
  response.write_uint64(ck1);
  response.write_uint64(ck2);
  response.write_uint64(ck3);

  // DEBUG("Got packet CK");
  // response.print_hex(true);
  //
  // DEBUG("Send packet CK");
  // retresponse.print_hex();

  send_event(response, port);
}

void rtppeer::send_ck0() {
  waiting_ck = true;
  uint64_t ck1 = get_timestamp();
  uint64_t ck2 = 0;
  uint64_t ck3 = 0;

  io_bytes_writer_static<36> response;
  response.write_uint16(0xFFFF);
  response.write_uint16(rtppeer::CK);
  response.write_uint32(local_ssrc);
  response.write_uint8(0);
  // padding
  response.write_uint8(0);
  response.write_uint16(0);
  // cks
  response.write_uint64(ck1);
  response.write_uint64(ck2);
  response.write_uint64(ck3);

  DEBUG("Send CK0 to {}", remote_name);

  // DEBUG("Got packet CK");
  // buffer.print_hex(true);
  //
  // DEBUG("Send packet CK");
  // retbuffer.print_hex();

  send_event(response, MIDI_PORT);
}

void rtppeer::parse_command_rs(io_bytes_reader &buffer, port_e port) {
  buffer.read_uint32(); // Ignore SSRC
  auto seqn = buffer.read_uint32();
  if (journal) {
    journal->seq_confirmed = seqn;
  }
}

void rtppeer::parse_midi(io_bytes_reader &buffer) {
  // auto _headers =
  buffer.read_uint8(); // Ignore RTP header flags (Byte 0)
  auto rtpmidi_id = buffer.read_uint8();
  if (rtpmidi_id != 0x61) { // next Byte: Payload type
    WARNING("Received packet which is not RTP MIDI. Ignoring.");
    return;
  }
  remote_seq_nr = buffer.read_uint16();
  // auto _remote_timestamp =
  buffer.read_uint32();                    // Ignore timestamp
  auto remote_ssrc = buffer.read_uint32(); // SSRC
  if (remote_ssrc != this->remote_ssrc) {
    WARNING("Got message for unknown remote SSRC on this port. (from {:04X}, "
            "I'm {:04X})",
            remote_ssrc, this->remote_ssrc);
    return;
  }

  // RFC 6295 RTP-MIDI _header
  // The Flags are:
  // B = has long header
  // J = has journal
  // Z = delta time on first MIDI-command present
  // P = no status byte in original midi command
  auto header = buffer.read_uint8();
  int16_t length = header & 0x0F;
  if ((header & 0x80) != 0) {
    length <<= 8;
    length += buffer.read_uint8();
    DEBUG("Long header, {} bytes long", length);
  }
  if ((header & 0x40) != 0) {
    // I actually parse the journal BEFORE the current message as it is
    // for events before the event.
    WARNING("This RTP MIDI header has journal. WIP.");
    io_bytes_reader journal_data = buffer;
    journal_data.position += length;

    if (journal) {
      journal->parse_journal(journal_data, midi_event);
    }
    send_feedback(remote_seq_nr);
  }
  if ((header & 0x20) != 0) {
    WARNING("This RTP MIDI payload has delta time for the first command. "
            "Ignoring.");
    buffer.read_uint8();
  }
  if ((header & 0x10) != 0) {
    WARNING("There was no status byte in original MIDI command. Ignoring.");
  }
  buffer.check_enough(length);

  io_bytes midi(buffer.position, length);
  midi_event(midi);
}

/**
 * Returns the times since start of this object in 100 us (1e-4) / 0.1 ms
 *
 * 10 ts = 1ms, 10000 ts = 1s. 1ms = 0.1ts
 */
uint64_t rtppeer::get_timestamp() {
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);
  // ns is 1e-9s. I need 1e-4s, so / 1e5
  uint64_t now = spec.tv_sec * 10000 + spec.tv_nsec / 1.0e5;
  // DEBUG("{}s {}ns", spec.tv_sec, spec.tv_nsec);

  return uint32_t(now - timestamp_start);
}

void rtppeer::send_midi(const io_bytes_reader &events) {
  if (!is_connected()) { // Not connected yet.
    DEBUG("Can not send MIDI data to {} yet, not connected ({:X}).",
          remote_name, (int)status);
    return;
  }

  io_bytes_writer_static<512> buffer;

  uint32_t timestamp = get_timestamp();
  seq_nr++;

  uint8_t has_journal_bit = journal->has_journal ? 0x40 : 0;
  buffer.write_uint8(0x80);
  buffer.write_uint8(0x61);
  buffer.write_uint16(seq_nr);
  buffer.write_uint32(timestamp);
  buffer.write_uint32(local_ssrc);

  auto size = events.size();
  if (size > 4095) {
    throw rtpmidid::exception("MIDI packet too big. Max size is 4096 bytes.");
  }
  if (size < 16) {
    buffer.write_uint8(size | has_journal_bit);

  } else {
    uint8_t sizeh = size << 8;
    uint8_t sizel = size & 0xFF;
    buffer.write_uint8(sizeh | has_journal_bit | 0x80); // mark long midi packet
    buffer.write_uint8(sizel);
  }
  // Now midi
  buffer.copy_from(events);

  // Now journal
  if (has_journal_bit)
    journal->write_journal(buffer);

  // events.print_hex();
  // buffer.print_hex();

  send_event(buffer, MIDI_PORT);
  if (journal) {
    journal->midi_in(seq_nr, events);
  }
}

void rtppeer::send_goodbye(port_e to_port) {
  io_bytes_writer_static<64> buffer;

  buffer.write_uint16(0x0FFFF);
  buffer.write_uint16(::rtppeer::BY);
  buffer.write_uint32(2);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(local_ssrc);

  send_event(buffer, to_port);

  status =
      status_e(int(status) &
               ~int(to_port == MIDI_PORT ? MIDI_CONNECTED : CONTROL_CONNECTED));

  if (status == NOT_CONNECTED) {
    disconnect_event(DISCONNECT);
  }
}

void rtppeer::send_feedback(uint32_t seqnum) {
  // uint8_t packet[256];

  DEBUG("Send feedback to the other end. Journal parsed. Seqnum {}", seqnum);
  remote_seq_nr = seqnum;
  io_bytes_writer_static<96> buffer;

  buffer.write_uint16(0xFFFF);
  buffer.write_uint16(rtppeer::RS);
  buffer.write_uint32(local_ssrc);
  buffer.write_uint32(seqnum);

  send_event(buffer, CONTROL_PORT);
}

/// Sends a packet with ONLY the journal, no MIDI data
void rtppeer::send_journal() {
  if (!journal)
    return;

  io_bytes_writer_static<512> buffer;
  uint32_t timestamp = get_timestamp();
  seq_nr++;

  buffer.write_uint8(0x81);
  buffer.write_uint8(0x61);
  buffer.write_uint16(seq_nr);
  buffer.write_uint32(timestamp);
  buffer.write_uint32(local_ssrc);
  buffer.write_uint8(0x40); // Only journal bit
  journal->write_journal(buffer);

  DEBUG("Journal packet ready: {} bytes", buffer.pos());
  send_event(buffer, CONTROL_PORT);
}

void rtppeer::connect_to(port_e rtp_port) {
  io_bytes_writer_static<1500> buffer;

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

  send_event(buffer, rtp_port);
}
