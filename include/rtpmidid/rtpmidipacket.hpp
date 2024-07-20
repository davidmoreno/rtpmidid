/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "packet.hpp"
#include <fmt/core.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace rtpmidid {

/**
 *      0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | V |P|X|  CC   |M|     PT      |        Sequence number        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                           Timestamp                           |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                             SSRC                              |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
class midi_packet_t : public packet_t {
public:
  midi_packet_t(uint8_t *data, size_t size) : packet_t(data, size) {}

  static bool is_midi_packet(const uint8_t *data, size_t size) {
    return midi_packet_t(const_cast<uint8_t *>(data), size).is_midi_packet();
  }

  bool is_midi_packet() const {
    if (size < 12)
      return false;
    if (data[0] == 0xff && data[1] == 0xff)
      return false;

    return (data[0] & 0b11000000) == 0b10000000;
  }
  int get_flag_v() const { return (data[0] & 0b11000000) >> 6; }
  bool get_flag_p() const { return (data[0] & 0b00100000) >> 5; }
  bool get_flag_x() const { return (data[0] & 0b00010000) >> 4; }
  int get_flag_cc() const { return (data[0] & 0b00001111); }
  int get_flag_m() const { return (data[1] & 0b10000000) >> 7; }
  int get_flag_pt() const { return (data[1] & 0b01111111); }
  int get_sequence_number() const { return (data[2] << 8) | data[3]; }
  uint32_t get_timestamp() const {
    return (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
  }
  int get_ssrc() const {
    return (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
  }
  std::string to_string() const {
    if (!is_midi_packet()) {
      std::string first_12_bytes_hex;
      for (int i = 0; i < 12; i++) {
        first_12_bytes_hex += fmt::format("{:02x} ", data[i]);
      }
      return fmt::format("RTP Packet: Invalid MIDI packet {}",
                         first_12_bytes_hex);
    }
    return fmt::format("RTP Packet: V:{} P:{} X:{} CC:{} M:{} PT:{} "
                       "Sequence:{} Timestamp:{} SSRC:{}",
                       get_flag_v(), get_flag_p(), get_flag_x(), get_flag_cc(),
                       get_flag_m(), get_flag_pt(), get_sequence_number(),
                       get_timestamp(), get_ssrc());
  }
};

/** Initiation and termination
 *      0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | 0xFF           | 0xFF          | Command                      |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                           Protocol version                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                             sender SSRC                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | name... (\0 terminated)                                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

/** Timestamp
 *      0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | 0xFF           | 0xFF          | 'C'           'K'            |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                             sender SSRC                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |  count         | unused                                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 1, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 1, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 2, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 2, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 3, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    | timestamp 3, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

enum command_e {
  IN = 0x494e,
  OK = 0x4f4b,
  NO = 0x4e4f,
  BY = 0x4259,
  CK = 0x434b,
  RS = 0x5253,
};

class command_packet_t : public packet_t {
public:
  command_packet_t(uint8_t *data, size_t size) : packet_t(data, size) {}

  static bool is_command_packet(const uint8_t *data, size_t size) {
    return command_packet_t(const_cast<uint8_t *>(data), size)
        .is_command_packet();
  }

  bool is_command_packet() const {
    if (size < 13)
      return false;

    return data[0] == 0xFF && data[1] == 0xFF;
  }
  command_e get_command() const {
    return (command_e)((data[2] << 8) | data[3]);
  }
  uint32_t get_protocol_version() const {
    return (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
  }
  uint32_t get_initiator_token() const {
    return (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
  }
  uint32_t get_sender_ssrc() const {
    // for IN and BY its at 12, for others, at 8
    auto type = get_command();
    if (type == IN || type == OK) {
      return (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15];
    }
    return (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
  }
  std::string get_name() const {
    // only for IN
    auto command = get_command();
    if (command != IN && command != OK) {
      return "";
    }
    return std::string((char *)&data[16]);
  }

  std::string to_string() const {
    if (!is_command_packet()) {
      std::string first_12_bytes_hex;
      for (int i = 0; i < 16; i++) {
        first_12_bytes_hex += fmt::format("{:02x} ", data[i]);
      }
      return fmt::format("RTP Command Packet: Invalid Command packet {}",
                         first_12_bytes_hex);
    }
    return fmt::format("RTP Command Packet: Command:{} Protocol:{} Initiator: "
                       "{} SSRC:{} Name:{}",
                       get_command(), get_protocol_version(),
                       get_initiator_token(), get_sender_ssrc(), get_name());
  }

  command_packet_t &initialize() {
    data[0] = 0xFF;
    data[1] = 0xFF;
    // protocol is 2
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 2;

    // rest set to 0
    memset(&data[8], 0, size - 8);
    return *this;
  }
  command_packet_t &set_command(command_e cmd) {
    data[2] = (cmd >> 8) & 0xFF;
    data[3] = cmd & 0xFF;
    return *this;
  }
  command_packet_t &set_protocol_version(uint32_t version) {
    data[4] = (version >> 24) & 0xFF;
    data[5] = (version >> 16) & 0xFF;
    data[6] = (version >> 8) & 0xFF;
    data[7] = version & 0xFF;
    return *this;
  }
  command_packet_t &set_initiator_token(uint32_t token) {
    data[8] = (token >> 24) & 0xFF;
    data[9] = (token >> 16) & 0xFF;
    data[10] = (token >> 8) & 0xFF;
    data[11] = token & 0xFF;
    return *this;
  }
  command_packet_t &set_sender_ssrc(uint32_t ssrc) {
    data[12] = (ssrc >> 24) & 0xFF;
    data[13] = (ssrc >> 16) & 0xFF;
    data[14] = (ssrc >> 8) & 0xFF;
    data[15] = ssrc & 0xFF;
    return *this;
  }
  command_packet_t &set_name(const std::string &name) {
    auto name_size = std::min(name.size(), size - 17);
    memcpy(&data[16], name.c_str(), name_size);
    data[16 + name_size] = 0;
    return *this;
  }

  size_t get_size_to_send() const {
    auto command = get_command();
    bool is_more_than_16_bytes = command == IN || command == OK;
    if (is_more_than_16_bytes) {
      return 16 + strlen((char *)&data[16]) + 1;
    } else {
      return 12;
    }
  }

  packet_t as_send_packet() const {
    auto final_size = get_size_to_send();
    return packet_t(data, final_size);
  }
};

} // namespace rtpmidid

// allow formating for command_e
template <> struct fmt::formatter<rtpmidid::command_e> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::command_e &c, FormatContext &ctx) {
    switch (c) {
    case rtpmidid::IN:
      return format_to(ctx.out(), "IN");
    case rtpmidid::OK:
      return format_to(ctx.out(), "OK");
    case rtpmidid::NO:
      return format_to(ctx.out(), "NO");
    case rtpmidid::BY:
      return format_to(ctx.out(), "BY");
    case rtpmidid::CK:
      return format_to(ctx.out(), "CK");
    case rtpmidid::RS:
      return format_to(ctx.out(), "RS");
    default:
      return format_to(ctx.out(), "Unknown Command:{}", c);
    }
  }
};

// allow fmt to format rtpmidid::midi_packet_t
template <> struct fmt::formatter<rtpmidid::midi_packet_t> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::midi_packet_t &p, FormatContext &ctx) {
    return format_to(ctx.out(), "{}", p.to_string());
  }
};

// allow fmt to format rtpmidid::command_packet_t
template <> struct fmt::formatter<rtpmidid::command_packet_t> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::command_packet_t &p, FormatContext &ctx) {
    return format_to(ctx.out(), "{}", p.to_string());
  }
};
