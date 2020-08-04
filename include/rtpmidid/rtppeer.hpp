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

#pragma once
#include "signal.hpp"
#include <arpa/inet.h>
#include <functional>
#include <string>

namespace rtpmidid {
class io_bytes_reader;
class io_bytes;

class rtppeer {
public:
  // Commands, the id is the same chars as the name
  enum commands_e {
    IN = 0x494e,
    OK = 0x4f4b,
    NO = 0x4e4f,
    BY = 0x4259,
    CK = 0x434b,
    RS = 0x5253
  };
  enum status_e {
    NOT_CONNECTED = 0,
    CONTROL_CONNECTED = 1,
    MIDI_CONNECTED = 2,
    CONNECTED = 3
  };
  enum port_e {
    MIDI_PORT,
    CONTROL_PORT,
  };
  enum disconnect_reason_e {
    CANT_CONNECT,
    PEER_DISCONNECTED,
    CONNECTION_REJECTED,
    DISCONNECT,
    CONNECT_TIMEOUT,
    CK_TIMEOUT,
  };

  status_e status;
  uint32_t initiator_id;
  uint32_t remote_ssrc;
  uint32_t local_ssrc;
  std::string local_name;
  std::string remote_name;
  uint16_t seq_nr_ack;
  uint16_t seq_nr;
  uint16_t remote_seq_nr;
  uint64_t timestamp_start; // Time in ms
  uint64_t latency;
  bool waiting_ck;

  /// Event for connected
  signal_t<const std::string &, status_e> connected_event;
  /// Event for disconnect
  signal_t<disconnect_reason_e> disconnect_event;
  /// Event for send MIDI.
  // It is const & to make automatic conversion from io_bytes_writer. Need the
  // const or the compiler thinks you are stupid and why give a object to
  // modify when you will not use it. At least it should be a value or a lvalue.
  // But what we need is the conversion. Actually you can force the non-const
  // reader and conversion at connect:
  // `midi_event.connect([](io_bytes_reader reader){})`
  // And everybody happy.
  signal_t<const io_bytes_reader &> midi_event;
  /// Event for send data to network.
  signal_t<const io_bytes_reader &, port_e> send_event;

  // Clock latency check received. in ms
  signal_t<float> ck_event;

  static bool is_command(io_bytes_reader &);
  static bool is_feedback(io_bytes_reader &);

  rtppeer(std::string _name);
  ~rtppeer();

  bool is_connected() { return status == CONNECTED; }
  void reset();
  void data_ready(io_bytes_reader &&, port_e port);

  void parse_command(io_bytes_reader &, port_e port);
  void parse_feedback(io_bytes_reader &);
  void parse_command_ok(io_bytes_reader &, port_e port);
  void parse_command_in(io_bytes_reader &, port_e port);
  void parse_command_ck(io_bytes_reader &, port_e port);
  void parse_command_by(io_bytes_reader &, port_e port);
  void parse_command_no(io_bytes_reader &, port_e port);
  void parse_midi(io_bytes_reader &);

  void send_midi(const io_bytes_reader &buffer);
  void send_goodbye(port_e to_port);
  void send_feedback(uint32_t seqnum);
  void connect_to(port_e rtp_port);
  void send_ck0();
  uint64_t get_timestamp();

  // Journal
  void parse_journal(io_bytes_reader &);
  void parse_journal_chapter(io_bytes_reader &);
  void parse_journal_chapter_N(uint8_t channel, io_bytes_reader &);
};
} // namespace rtpmidid
