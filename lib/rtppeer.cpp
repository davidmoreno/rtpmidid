/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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
#include <rtpmidid/utils.hpp>

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
rtppeer_t::rtppeer_t(std::string _name)
    : local_ssrc(::rtpmidid::rand_u32() & 0x0FFFF),
      local_name(std::move(_name)), seq_nr(::rtpmidid::rand_u32() & 0x0FFFF) {
  timestamp_start = get_timestamp();

  seq_nr_ack = seq_nr;
}

// NOLINTNEXTLINE(bugprone-exception-escape) - If it happens, it is a bug
rtppeer_t::~rtppeer_t() {
  if (status == CONNECTED) {
    send_goodbye(CONTROL_PORT);
    send_goodbye(MIDI_PORT);
  }
  DEBUG("~rtppeer '{}' (local) <-> '{}' (remote)", local_name, remote_name);
}

void rtppeer_t::reset() {
  status = NOT_CONNECTED;
  remote_name = "";
  remote_ssrc = 0;
  initiator_id = 0;
  // A session must not end with notes sounding: if we believe a note is on, its
  // NoteOff either never arrived or never will. This covers disconnects,
  // network deaths and the peer going away, as every teardown path ends here.
  recovery_journal.session_end(midi_event);
  recovery_journal.reset();
}

void rtppeer_t::data_ready(io_bytes_reader &&buffer, port_e port) {
  if (port == CONTROL_PORT) {
    if (is_command(buffer)) {
      parse_command(buffer, port);
    } else if (is_feedback(buffer)) {
      parse_feedback(buffer);
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

bool rtppeer_t::is_command(io_bytes_reader &pb) {
  // DEBUG("Is command? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF,
  // pb.start[1] == 0xFF);
  return (pb.size() >= 16 && pb.start[0] == 0xFF && pb.start[1] == 0xFF);
}
bool rtppeer_t::is_feedback(io_bytes_reader &pb) {
  // DEBUG("Is feedback? {} {} {}", pb.size() >= 16, pb.start[0] == 0xFF,
  // pb.start[1] == 0xFF);
  return (pb.size() >= 12 && pb.start[0] == 0xFF && pb.start[1] == 0xFF &&
          pb.start[2] == 0x52 && pb.start[3] == 0x53);
}

void rtppeer_t::parse_command(io_bytes_reader &buffer, port_e port) {
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
  case rtppeer_t::OK:
    parse_command_ok(buffer, port);
    break;
  case rtppeer_t::IN:
    parse_command_in(buffer, port);
    break;
  case rtppeer_t::CK:
    parse_command_ck(buffer, port);
    break;
  case rtppeer_t::BY:
    parse_command_by(buffer, port);
    break;
  case rtppeer_t::NO:
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
void rtppeer_t::parse_command_ok(io_bytes_reader &buffer, port_e port) {
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

  INFO("Got confirmation from {}, initiator_id: {} ({}) ssrc: {}, "
       "port: {}",
       remote_name, initiator_id,
       this->initiator_id == initiator_id ? "ok" : "nok", remote_ssrc, port);

  if (port == MIDI_PORT) {
    status = status_e(int(status) | int(MIDI_CONNECTED));
  } else if (port == CONTROL_PORT) {
    status = status_e(int(status) | int(CONTROL_CONNECTED));
  } else {
    ERROR("Got data on unknown PORT! {}", port);
  }
  DEBUG("New status is {}", status);
  status_change_event(status);
}

/**
 * This command is received when I am a server.
 *
 * A client sent me IN, I answer with OK, and we are set.
 *
 * TODO It would be super nice to be able to have several clients
 * connected to me.
 */
void rtppeer_t::parse_command_in(io_bytes_reader &buffer, port_e port) {
  if (status == CONNECTED) {
    WARNING("This peer is already connected. But OK, I will accept it anew. "
            "Might happen on some split brain situations.");
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

  INFO("Got connection request from remote_name=\"{}\", initiator_id={:X}  "
       "ssrc={:X}, local_name=\"{}\", at port={}",
       remote_name, initiator_id, remote_ssrc, remote_name, port);

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

  status_change_event(status);
}

void rtppeer_t::parse_command_by(io_bytes_reader &buffer, port_e port) {
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

  uint8_t mask = 0xFF;

  if (port == MIDI_PORT)
    mask = ~MIDI_CONNECTED;
  if (port == CONTROL_PORT)
    mask = ~CONTROL_CONNECTED;
  auto nextstatus = status_e(int(status) & mask);

  DEBUG("Parse BY. status: {}, port {}| {:08b} & {:08b} = {:08b}", status, port,
        (uint8_t)(status), (uint8_t)mask, int(status) & mask);

  INFO("Disconnect from {}, {} port. Status {} -> {}", remote_name, port,
       status, nextstatus);
  status = nextstatus;

  // One BY is enough, not even waiting for the midi one.
  if (status == NOT_CONNECTED)
    status_change_event(DISCONNECTED_PEER_DISCONNECTED);
}

void rtppeer_t::parse_command_no(io_bytes_reader &buffer, port_e port) {
  auto protocol = buffer.read_uint32();
  initiator_id = buffer.read_uint32();
  [[maybe_unused]] auto remote_ssrc = buffer.read_uint32();

  if (protocol != 2) {
    throw exception(
        "rtpmidid only understands RTP MIDI protocol 2. Fill an issue at "
        "https://github.com/davidmoreno/rtpmidid/. Got protocol {}",
        protocol);
  }

  status = (status_e)(((int)status) &
                      ~((int)(port == MIDI_PORT ? MIDI_CONNECTED
                                                : CONTROL_CONNECTED)));
  WARNING("Invitation Rejected (NO) : remote ssrc {:X}", remote_ssrc);
  INFO("Disconnect from {}, {} port. Status {:X}", remote_name,
       port == MIDI_PORT ? "MIDI" : "Control", (int)status);

  status_change_event(DISCONNECTED_CONNECTION_REJECTED);
}

void rtppeer_t::parse_command_ck(io_bytes_reader &buffer, port_e port) {
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
    INFO("Latency {}: {:.2f} ms (client / 2)", std::string_view(remote_name),
         latency / 10.0);
    ck_event(float(latency) / 10.0f);
    stats.add_stat(std::chrono::nanoseconds((int)latency * 100));
  } break;
  case 2: {
    // Receive the other side CK, I can calculate latency
    ck2 = buffer.read_uint64();
    // ck3 = buffer.read_uint64();
    latency = get_timestamp() - ck2;
    INFO("Latency {}: {:.2f} ms (server / 3)", std::string_view(remote_name),
         latency / 10.0);
    // No need to send message
    stats.add_stat(std::chrono::nanoseconds((int)latency * 100));
    ck_event(float(latency) / 10.0f);
    return;
  }
  default:
    ERROR("Bad CK count. Ignoring.");
    return;
  }

  io_bytes_writer_static<36> response;
  response.write_uint16(0xFFFF);
  response.write_uint16(rtppeer_t::CK);
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

void rtppeer_t::send_ck0() {
  waiting_ck = true;
  uint64_t ck1 = get_timestamp();
  uint64_t ck2 = 0;
  uint64_t ck3 = 0;

  io_bytes_writer_static<36> response;
  response.write_uint16(0xFFFF);
  response.write_uint16(rtppeer_t::CK);
  response.write_uint32(local_ssrc);
  response.write_uint8(0);
  // padding
  response.write_uint8(0);
  response.write_uint16(0);
  // cks
  response.write_uint64(ck1);
  response.write_uint64(ck2);
  response.write_uint64(ck3);

  // DEBUG("Send CK0 to {}", std::string_view(remote_name));

  // DEBUG("Got packet CK");
  // buffer.print_hex(true);
  //

  send_event(response, MIDI_PORT);
}

void rtppeer_t::parse_feedback(io_bytes_reader &buffer) {
  // Apple's journal feedback packet: 0xFFFF, 'RS', SSRC (4 octets), and the
  // extended sequence number of the most recently received packet as a 32 bit
  // value. Reading only 16 bits here (as this used to) always yields 0.
  buffer.position = buffer.start + 8;
  auto remote_seq_nr_ack = buffer.read_uint32();
  seq_nr_ack = uint16_t(remote_seq_nr_ack & 0xFFFF);
  recovery_journal.feedback_in(remote_seq_nr_ack);
}

int rtppeer_t::next_midi_packet_length(io_bytes_reader &buffer) {
  // Get length depending on midi event
  buffer.check_enough(1);
  auto status = *buffer.position;
  int length = 0;

  // Update running status
  // RealTime Category messages (0xF8 to 0xFF) do nothing to the running status
  // state
  if (0xF0 <= status &&
      status <= 0xF7) { // System Common and System Exclusive messages
    running_status = 0; // cancel the running status state.
  } else if (0x80 <= status && status < 0xF0) { // Voice Category messages
    running_status = status;  // update the runnning status state.
  } else if (status < 0x80) { // Abbreviated commands
    status = running_status;  // use the running status state
    length = -1;              // and are 1 byte shorter.
  }

  auto first_byte_f0 = status & 0xF0;
  switch (first_byte_f0) {
  case 0x80:
  case 0x90:
  case 0xA0:
  case 0xB0:
  case 0xE0:
    length += 3;
    break;
  case 0xC0:
  case 0xD0:
    length += 2;
    break;
  }
  if (length <= 0) {
    switch (status) {
    case 0xF6: // Tune request
    case 0xF8: // Clock message (24 ticks per quarter note)
    case 0xF9: // Tick (some master devices send once every 10ms)
    case 0xFA: // Start
    case 0xFB: // Continue
    case 0xFC: // Stop
    case 0xFE: // Active sense (ping every 300ms or so, to advice still
               // connected)
    case 0xFF: // Reset (Panic)
      length = 1;
      break;
    case 0xF1: // MTC Quarter frame
    case 0xF3: // Song select
      length = 2;
      break;
    case 0xF2: // Song position pointer
      length = 3;
      break;
    case 0xF0: // SysEX start
    case 0xF7: // SysEX end
    case 0xF4: // SysEX continue
      length = 2;
      auto byte = buffer.position + 1;
      while ((*byte & 0x80) == 0x00) {
        byte++;
        if (byte >= buffer.end) {
          throw bad_midi_packet("Unexpected SysEx packet end");
        }
        length++;
      }
      // DEBUG("Check sysex length: {}", length);
      break;
    }
  }
  return length;
}

/**
 * Read and decode delta time from a RFC 6295 MIDI List Structure.
 *
 * Returns the number of consumed bytes (1-4).
 * buffer (in/out): the buffer from which to consume data (position is updated)
 * delta_time (out): the decoded delta time
 */
int rtppeer_t::read_delta_time(io_bytes_reader &buffer, uint32_t &delta_time) {
  uint8_t delta_byte = buffer.read_uint8();
  int nb_read = 1;
  delta_time = delta_byte & 0x7F;
  // Just look for first bit that will mark if more bytes
  while ((delta_byte & 0x80) != 0x00) {
    delta_byte = buffer.read_uint8();
    delta_time <<= 7;
    delta_time |= delta_byte & 0x7F;
    nb_read++;
  }
  return nb_read;
}

void rtppeer_t::parse_midi(io_bytes_reader &buffer) {
  // auto _headers =
  buffer.read_uint8(); // Ignore RTP header flags (Byte 0)
  auto rtpmidi_id = buffer.read_uint8() & 0x7f;
  if (rtpmidi_id != 0x61) { // next Byte: Payload type
    WARNING("Received packet (ID: 0x{:02x}) which is not RTP MIDI. Ignoring.",
            rtpmidi_id);
    buffer.print_hex();
    return;
  }
  remote_seq_nr = buffer.read_uint16(); // Ignore RTP sequence no.
  // TODO In the future we may use a journal.
  // auto _remote_timestamp =
  buffer.read_uint32();                    // Ignore timestamp
  auto remote_ssrc = buffer.read_uint32(); // SSRC
  if (remote_ssrc != this->remote_ssrc) {
    WARNING("Got message for unknown remote SSRC on this port. (from {:04X}, "
            "I'm {:04X})",
            remote_ssrc, this->remote_ssrc);
    return;
  }

  // Sequence tracking for the recovery journal: classify this packet against
  // the ones received before it, before anything from it is played.
  auto loss = recovery_journal.observe(remote_seq_nr);

  // RFC 6295 RTP-MIDI _header
  // The Flags are:
  // B = has long header
  // J = has journal
  // Z = delta time on first MIDI-command present
  // P = no status byte in original midi command
  uint8_t header = buffer.read_uint8();
  int length = header & 0x0F;
  if ((header & 0x80) != 0) {
    length <<= 8;
    length += buffer.read_uint8();
    // DEBUG("Long header, {} bytes long", length);
  }
  buffer.check_enough(length);
  auto remaining = length;

  if ((header & 0x40) != 0) {
    // The journal describes the packets before this one, so it is applied
    // before the events of this packet: a recovered NoteOff must land before a
    // new NoteOn for the same note.
    io_bytes_reader journal_data = buffer;
    journal_data.position += length;

    recovery_journal.parse_journal(journal_data, loss,
                                   uint32_t(get_timestamp()), midi_event);
  }
  if ((header & 0x20) != 0) {
    WARNING("This RTP MIDI payload has delta time for the first command. "
            "Ignoring.");
    uint32_t delta_time = 0;
    remaining -= read_delta_time(buffer, delta_time);
  }
  if ((header & 0x10) != 0) {
    WARNING("There was no status byte in original MIDI command. Ignoring.");
  }

  // Parse MIDI list structure
  // (May be several midi messages with delta time)

  // The first MIDI channel command in the MIDI list MUST include a status
  // octet. (RFC 6295, p.16)
  running_status = 0;

  while (remaining) {
    length = next_midi_packet_length(buffer);
    if (length == 0) {
      throw bad_midi_packet(
          FMT::format("Unexpected MIDI data: {}", *buffer.position).c_str());
    }
    buffer.check_enough(length);
    // DEBUG("Remaining {}, length for this packet: {}", remaining, length);
    remaining -= length;

    if (!sysex.empty() || *buffer.position == 0xF0) {
      parse_sysex(buffer, int16_t(length));
    } else if (*buffer.position < 0x80 && running_status) {
      // Abbreviated midi message using running status
      io_bytes_managed midi(length + 1);
      io_bytes_writer midi_writer(midi);
      midi_writer.write_uint8(running_status);
      midi_writer.copy_from(buffer.position, length);
      emit_midi(midi);
    } else {
      // Normal flow, simple midi data
      io_bytes midi(buffer.position, length);
      emit_midi(midi);
    }
    buffer.skip(length);

    // DEBUG("Remaining: {}, size: {}, left: {}", remaining, buffer.size(),
    //       buffer.size() - buffer.pos());

    if (remaining) {
      // DEBUG("Packet with several midi events. {} bytes remaining",
      // remaining); Skip delta
      uint32_t delta_time = 0;
      remaining -= read_delta_time(buffer, delta_time);
      // DEBUG("Skip delta_time: {}", delta_time);
    }
  }
}

void rtppeer_t::parse_sysex(io_bytes_reader &buffer, int16_t length) {
  // buffer.print_hex();
  auto last_byte = *(buffer.position + length - 1);

  if (!sysex.empty()) {
    // DEBUG("Read SysEx cont. {:x} ... {:x} ({:p})", *buffer.position,
    // last_byte, buffer.position);
    if (*buffer.position != 0xF7) {
      throw rtpmidid::bad_sysex_exception("Next packet does not start with F7");
    }
    std::copy(buffer.position + 1, buffer.position + length - 1,
              std::back_inserter(sysex));
    // DEBUG("Sysex size: {}", sysex.size());
    // io_bytes(&sysex[0], sysex.size()).print_hex();

    // Final packet
    switch (last_byte) {
    case 0xF7: {
      sysex.push_back(0xF7);
      if (sysex.size() == 3) {
        WARNING("NOT Sending empty SysEx packet");
        sysex.clear();
        return;
      }
      auto sysexreader = io_bytes_reader(&sysex[1], sysex.size() - 1);
      // DEBUG("Send sysex {}", sysex.size());
      // sysexreader.print_hex();
      emit_midi(sysexreader);
      sysex.clear();

    } break;
    case 0xF4:
      // Cancel.. just clear sysex
      sysex.clear();
      break;
    case 0xF0:
      // Continue, do nothing (data already copied before)
      break;
    default:
      WARNING("Bad sysex end byte: {}", last_byte);
      throw rtpmidid::bad_sysex_exception("Bad sysex end byte");
    }
  } else if (*buffer.position == 0xF0) {
    // DEBUG("Read SysEx. {:x} ... {:x} ({:p})", *buffer.position, last_byte,
    //       buffer.position);
    if (last_byte == 0xF7) { // Normal packet
      // DEBUG("Read normal sysex packet");
      io_bytes midi(buffer.position, length);
      emit_midi(midi);
    } else {
      // DEBUG("First part");
      sysex.clear();
      std::copy(buffer.position - 1, buffer.position + length - 1,
                std::back_inserter(sysex));
    }
  }
}

/**
 * Returns the times since start of this object in 100 us (1e-4) / 0.1 ms
 *
 * 10 ts = 1ms, 10000 ts = 1s. 1ms = 0.1ts
 */
uint64_t rtppeer_t::get_timestamp() {
  struct timespec spec{};

  clock_gettime(CLOCK_MONOTONIC, &spec);
  // ns is 1e-9s. I need 1e-4s, so / 1e5
  uint64_t now =
      uint64_t(spec.tv_sec * 10000) + uint64_t(double(spec.tv_nsec) / 1.0e5);
  // DEBUG("{}s {}ns", spec.tv_sec, spec.tv_nsec);

  return (now - timestamp_start);
}

void rtppeer_t::emit_midi(const io_bytes_reader &events) {
  recovery_journal.midi_played(events, uint32_t(get_timestamp()));
  midi_event(events);
}

void rtppeer_t::send_midi(const io_bytes_reader &events) {
  if (!is_connected()) { // Not connected yet.
    WARNING_RATE_LIMIT(
        10, "Can not send MIDI data to {} yet, not connected ({:X}).",
        remote_name, (int)status);
    return;
  }
  send_midi_packet(events);
  midi_sent_event();
}

bool rtppeer_t::send_journal_packet() {
  if (!is_connected() || !recovery_journal.enabled) {
    return false;
  }
  // A guard packet: empty MIDI command list, journal section. It does not fire
  // midi_sent_event, so it is not mistaken for MIDI activity.
  uint8_t empty_event = 0;
  send_midi_packet(io_bytes_reader(&empty_event, 0));
  recovery_journal.stats.guard_packets++;
  return true;
}

void rtppeer_t::send_midi_packet(const io_bytes_reader &events) {
  io_bytes_writer_static<4096 + 12> buffer;

  uint32_t timestamp = get_timestamp();
  seq_nr++;

  buffer.write_uint8(0x80);

  // Here it SHOULD send 0x80 | 0x61 if there is midi data sent, but if done,
  // then rtpmidi for windows does not read messages, so for compatibility,
  // just send 0x61
  buffer.write_uint8(0x61);
  buffer.write_uint16(seq_nr);
  buffer.write_uint32(timestamp);
  buffer.write_uint32(local_ssrc);

  auto events_size = events.size();
  // A stream that uses the recovery journal sets J in every payload, even when
  // the journal section is only its 3 octet header (RFC 6295 Section 2.2).
  const uint8_t journal_bit = recovery_journal.enabled ? 0x40 : 0x00;
  // Now midi
  if (events_size < 16) {
    // Short header, 1 octet
    buffer.write_uint8(events_size | journal_bit);
  } else {
    // Long header, 2 octets
    // DEBUG("Send long message: message_size={} bytes", events_size);
    if (events_size > 1400) {
      WARNING("MIDI message too long, may fail on UDP. message_size={} . If "
              "find any problem report an issue to "
              "http://github.com/davidmoreno/rtpmidid/ .",
              events_size);
    }

    buffer.write_uint8(((events_size & 0x0f00) >> 8) | 0x80 | journal_bit);
    buffer.write_uint8(events_size & 0xff);
  }

  buffer.copy_from(events);

  // The journal codes the history *before* this packet, so it is written before
  // the commands of this packet are recorded.
  if (recovery_journal.enabled) {
    recovery_journal.write_journal(buffer);
  }
  recovery_journal.midi_out(seq_nr, events);

  // events.print_hex();
  // buffer.print_hex();

  send_event(buffer, MIDI_PORT);
}

void rtppeer_t::send_goodbye(port_e to_port) {
  DEBUG("Send goodbye to {}", to_port);
  io_bytes_writer_static<64> buffer;

  buffer.write_uint16(0x0FFFF);
  buffer.write_uint16(::rtppeer_t::BY);
  buffer.write_uint32(2);
  buffer.write_uint32(initiator_id);
  buffer.write_uint32(local_ssrc);

  send_event(buffer, to_port);

  // Keep track of state
  if (status == CONNECTED) {
    if (to_port == MIDI_PORT)
      status = CONTROL_CONNECTED;
    else
      status = MIDI_CONNECTED;
  }
  if (status == MIDI_CONNECTED && to_port == MIDI_PORT)
    status = NOT_CONNECTED;
  if (status == CONTROL_CONNECTED && to_port == CONTROL_PORT)
    status = NOT_CONNECTED;

  if (status == NOT_CONNECTED) {
    DEBUG("Sent both goodbyes and is peer is disconected ({})", remote_name);
    status_change_event(DISCONNECTED_DISCONNECT);
  }
}

void rtppeer_t::send_feedback() {
  if (!is_connected() || !recovery_journal.has_received_packet()) {
    return;
  }
  // The receiver feedback packet: 0xFFFF, 'RS', SSRC, and the extended sequence
  // number of the most recently received packet, as a 32 bit value. It lets the
  // peer shrink its checkpoint history and stop sending guard packets.
  uint32_t seqnum = recovery_journal.highest_received_extended_seq();

  io_bytes_writer_static<96> buffer;
  buffer.write_uint16(0xFFFF);
  buffer.write_uint16(rtppeer_t::RS);
  buffer.write_uint32(local_ssrc);
  buffer.write_uint32(seqnum);

  recovery_journal.stats.feedback_sent++;
  send_event(buffer, CONTROL_PORT);
}

void rtppeer_t::connect_to(port_e rtp_port) {
  io_bytes_writer_static<1500> buffer;

  auto signature = 0xFFFF;
  auto command = rtppeer_t::IN;
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

void rtppeer_t::disconnect() {
  if (status & MIDI_CONNECTED) {
    send_goodbye(MIDI_PORT);
  }
  if (status & CONTROL_CONNECTED) {
    send_goodbye(CONTROL_PORT);
  }
  reset();
}
