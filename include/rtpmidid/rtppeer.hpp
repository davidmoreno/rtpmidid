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

#pragma once
#include "exceptions.hpp"
#include "networkaddress.hpp"
#include "signal.hpp"
#include "stats.hpp"
#include <arpa/inet.h>
#include <string>

namespace rtpmidid {
class io_bytes_reader;
class io_bytes;

class bad_sysex_exception : public ::rtpmidid::exception {
public:
  bad_sysex_exception(const char *what)
      : ::rtpmidid::exception("Bad SysEx: {}", what) {}
};

class bad_midi_packet : public ::rtpmidid::exception {
public:
  bad_midi_packet(const char *what)
      : ::rtpmidid::exception("Bad MIDI packet: {}", what) {}
};

class rtppeer_t {
  NON_COPYABLE_NOR_MOVABLE(rtppeer_t)
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
    CONNECTED = 3,

    DISCONNECTED =
        128, // to disconnect checks all are >= 128, so & 0x80 is true
    DISCONNECTED_CANT_CONNECT,        //
    DISCONNECTED_PEER_DISCONNECTED,   // Peer disconnected, BY
    DISCONNECTED_CONNECTION_REJECTED, // Connect not accepted, NO
    DISCONNECTED_DISCONNECT,          // Disconnect requested, BY
    DISCONNECTED_CONNECT_TIMEOUT,     // Timeout trying to connect
    DISCONNECTED_CK_TIMEOUT,          // Sent CK0, but no answer
    DISCONNECTED_NETWORK_ERROR,       // Network error, like no data received
  };

  enum port_e {
    MIDI_PORT,
    CONTROL_PORT,
  };

  status_e status = NOT_CONNECTED;
  uint32_t initiator_id = 0;
  uint32_t remote_ssrc = 0;
  uint32_t local_ssrc = 0;
  std::string local_name;
  std::string remote_name;
  uint16_t seq_nr_ack = 0;
  uint16_t seq_nr = 0;
  uint16_t remote_seq_nr = 0;
  uint64_t timestamp_start = 0; // Time in ms
  uint64_t latency = 0;
  bool waiting_ck = false;
  uint8_t running_status = 0;
  // Need some buffer space for sysex. This may require memory alloc.
  std::vector<uint8_t> sysex;
  stats_t stats;

  // This is to be filled at connection by whoever makes it
  // It is not used by the rtppeer_t, just nice info to have
  network_address_t remote_address;
  network_address_t local_address;

  typedef signal_t<status_e> status_change_event_t;
  /// Event for connected
  status_change_event_t status_change_event;

  typedef signal_t<const io_bytes_reader &> midi_event_t;
  /// Event for send MIDI.
  // It is const & to make automatic conversion from io_bytes_writer. Need the
  // const or the compiler thinks you are stupid and why give a object to
  // modify when you will not use it. At least it should be a value or a lvalue.
  // But what we need is the conversion. Actually you can force the non-const
  // reader and conversion at connect:
  // `midi_event.connect([](io_bytes_reader reader){})`
  // And everybody happy.
  midi_event_t midi_event;
  typedef signal_t<const io_bytes_reader &, port_e> send_event_t;
  /// Event for send data to network.
  send_event_t send_event;

  // Clock latency check received. in ms
  typedef signal_t<float> ck_event_t;
  ck_event_t ck_event;

  static bool is_command(io_bytes_reader &);
  static bool is_feedback(io_bytes_reader &);

  rtppeer_t(std::string _name);
  ~rtppeer_t(); // NOLINT(bugprone-exception-escape)

  bool is_connected() { return status == CONNECTED; }
  bool is_disconnected() { return status & DISCONNECTED; }
  static bool is_disconnected(status_e st) { return st & DISCONNECTED; }
  void reset();
  void data_ready(io_bytes_reader &&, port_e port);
  void disconnect();

  void parse_command(io_bytes_reader &, port_e port);
  void parse_feedback(io_bytes_reader &);
  void parse_command_ok(io_bytes_reader &, port_e port);
  void parse_command_in(io_bytes_reader &, port_e port);
  void parse_command_ck(io_bytes_reader &, port_e port);
  void parse_command_by(io_bytes_reader &, port_e port);
  void parse_command_no(io_bytes_reader &, port_e port);
  static int read_delta_time(io_bytes_reader &, uint32_t &delta_time);
  int next_midi_packet_length(io_bytes_reader &);
  void parse_midi(io_bytes_reader &);
  void parse_sysex(io_bytes_reader &, int16_t length);

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

template <>
struct fmt::formatter<rtpmidid::rtppeer_t::status_e>
    : formatter<fmt::string_view> {
  auto format(rtpmidid::rtppeer_t::status_e c, format_context &ctx) {
    fmt::string_view name = "UNKNOWN";
    switch (c) {
    case rtpmidid::rtppeer_t::status_e::NOT_CONNECTED:
      name = "NOT_CONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::CONTROL_CONNECTED:
      name = "CONTROL_CONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::MIDI_CONNECTED:
      name = "MIDI_CONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::CONNECTED:
      name = "CONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED:
      name = "DISCONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_CANT_CONNECT:
      name = "DISCONNECTED_CANT_CONNECT";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_PEER_DISCONNECTED:
      name = "DISCONNECTED_PEER_DISCONNECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_CONNECTION_REJECTED:
      name = "DISCONNECTED_CONNECTION_REJECTED";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_DISCONNECT:
      name = "DISCONNECTED_DISCONNECT";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_CONNECT_TIMEOUT:
      name = "DISCONNECTED_CONNECT_TIMEOUT";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_CK_TIMEOUT:
      name = "DISCONNECTED_CK_TIMEOUT";
      break;
    case rtpmidid::rtppeer_t::status_e::DISCONNECTED_NETWORK_ERROR:
      name = "DISCONNECTED_NETWORK_ERROR";
      break;
    }
    return formatter<fmt::string_view>::format(name, ctx); // NOLINT
  }
};

template <>
struct fmt::formatter<rtpmidid::rtppeer_t::port_e>
    : formatter<fmt::string_view> {
  auto format(rtpmidid::rtppeer_t::port_e c, format_context &ctx) {
    const char *name = "UNKNOWN"; // NOLINT
    switch (c) {
    case rtpmidid::rtppeer_t::port_e::MIDI_PORT:
      name = "MIDI_PORT";
      break;
    case rtpmidid::rtppeer_t::port_e::CONTROL_PORT:
      name = "CONTROL_PORT";
      break;
    }
    return formatter<fmt::string_view>::format(name, ctx); // NOLINT
  }
};
