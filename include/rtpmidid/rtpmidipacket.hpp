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

struct midi_event_t : public packet_t {
public:
  midi_event_t(uint8_t *start, size_t max_size) : packet_t(start, max_size) {
    // First have max_size at size, later the proper size
    size = get_event_size();
  }

  size_t get_event_size();
};

class midi_event_list_t : public packet_t {
public:
  class iterator_t : public packet_t {
  public:
    iterator_t(uint8_t *data, size_t size) : packet_t(data, size) {}
    bool operator!=(const iterator_t &other) {
      DEBUG("!= {} {}", (void *)data, (void *)other.data);
      return data != other.data;
    }
    midi_event_t operator*() { return midi_event_t(data, size); }
    iterator_t &operator++() {
      auto gsize = midi_event_t{data, size}.get_size();
      data += gsize;
      size -= gsize;
      return *this;
    }
  };

  midi_event_list_t(uint8_t *data, size_t size) : packet_t(data, size) {}

  iterator_t begin() { return iterator_t{data, size}; }
  iterator_t end() { return iterator_t{data + size, 0}; }
};

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
class packet_midi_t : public packet_t {
public:
  packet_midi_t(uint8_t *data, size_t size) : packet_t(data, size) {}

  static bool is_midi_packet(const uint8_t *data, size_t size) {
    return packet_midi_t(const_cast<uint8_t *>(data), size).is_midi_packet();
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

  midi_event_list_t get_midi_events();

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
                       "Sequence:{} Timestamp:{} SSRC:0x{:08X}",
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

enum command_e {
  IN = 0x494e,
  OK = 0x4f4b,
  NO = 0x4e4f,
  BY = 0x4259,
  CK = 0x434b,
  RS = 0x5253,
};

class packet_command_t : public packet_t {
public:
  packet_command_t(const packet_t &packet) : packet_t(packet) {}
  packet_command_t(uint8_t *data, size_t size) : packet_t(data, size) {}

  static bool is_command_packet(const uint8_t *data, size_t size) {
    return packet_command_t(const_cast<uint8_t *>(data), size)
        .is_command_packet();
  }

  bool is_command_packet() const {
    if (size < 13)
      return false;

    return data[0] == 0xFF && data[1] == 0xFF;
  }
  command_e get_command() const { return (command_e)(get_uint16(2)); }
  uint32_t get_protocol_version() const { return get_uint32(4); }
  uint32_t get_sender_ssrc() const {
    // for IN and BY its at 12, for others, at 8. Should be at child class, but
    // thiis allows some cases of inheritance and need for virtual functions
    auto type = get_command();
    if (type == IN || type == OK) {
      return get_uint32(12);
    }
    return get_uint32(8);
  }

  packet_command_t &initialize() {
    data[0] = 0xFF;
    data[1] = 0xFF;
    // protocol is 2
    set_uint32(4, 2);

    // rest set to 0
    memset(&data[8], 0, size - 8);
    return *this;
  }

  packet_command_t &set_command(command_e cmd) {
    set_uint16(2, cmd);
    return *this;
  }

  std::string to_string() const {
    if (!is_command_packet()) {
      std::string first_12_bytes_hex;
      for (int i = 0; i < 12; i++) {
        first_12_bytes_hex += fmt::format("{:02x} ", data[i]);
      }
      return fmt::format("RTP Command Packet: Invalid Command packet {}",
                         first_12_bytes_hex);
    }
    return fmt::format("RTP Command Packet: Command:{} Protocol:{} SSRC:{}",
                       get_command(), get_protocol_version(),
                       get_sender_ssrc());
  }
};

class packet_command_in_ok_t : public packet_command_t {
public:
  packet_command_in_ok_t(const packet_t &packet) : packet_command_t(packet) {}
  packet_command_in_ok_t(uint8_t *data, size_t size)
      : packet_command_t(data, size) {}

  uint32_t get_initiator_token() const { return get_uint32(8); }
  std::string get_name() const {
    // only for IN
    auto command = get_command();
    if (command != IN && command != OK) {
      return "";
    }
    return std::string((char *)&data[16]);
  }

  packet_command_in_ok_t &initialize(command_e cmd) {
    packet_command_t::initialize();
    packet_command_t::set_command(cmd);
    return *this;
  }

  packet_command_in_ok_t &set_initiator_token(uint32_t token) {
    set_uint32(8, token);
    return *this;
  }
  packet_command_in_ok_t &set_sender_ssrc(uint32_t ssrc) {
    set_uint32(12, ssrc);
    return *this;
  }
  packet_command_in_ok_t &set_name(const std::string &name) {
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
};

/** Timestamp
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0 | 0xFF           | 0xFF          | 'C'           'K'            |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  4 |                             sender SSRC                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  8 |  count         | unused                                       |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 12 | timestamp 1, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 16 | timestamp 1, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 20 | timestamp 2, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 24 | timestamp 2, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 28 | timestamp 3, high 32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 32 | timestamp 3, low  32 bits, in 0.1ms units                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

class packet_command_ck_t : public packet_command_t {
public:
  packet_command_ck_t(const packet_t &packet) : packet_command_t(packet) {}
  packet_command_ck_t(uint8_t *data, size_t size)
      : packet_command_t(data, size) {}

  uint8_t get_count() const { return data[8]; }
  uint64_t get_ck0() const { return get_uint64(12); }
  uint64_t get_ck1() const { return get_uint64(20); }
  uint64_t get_ck2() const { return get_uint64(28); }

  packet_command_ck_t &initialize() {
    packet_command_t::initialize();
    set_command(CK);
    return *this;
  }

  packet_command_ck_t &set_sender_ssrc(uint32_t ssrc) {
    set_uint32(4, ssrc);
    return *this;
  }

  packet_command_ck_t &set_count(uint8_t count) {
    set_uint8(8, count);
    return *this;
  }

  packet_command_ck_t &set_ck0(uint64_t timestamp) {
    set_uint64(12, timestamp);
    return *this;
  }
  packet_command_ck_t &set_ck1(uint64_t timestamp) {
    set_uint64(20, timestamp);
    return *this;
  }
  packet_command_ck_t &set_ck2(uint64_t timestamp) {
    set_uint64(28, timestamp);
    return *this;
  }

  std::string to_string() const {
    if (!is_command_packet()) {
      std::string first_12_bytes_hex;
      for (int i = 0; i < 12; i++) {
        first_12_bytes_hex += fmt::format("{:02x} ", data[i]);
      }
      return fmt::format("RTP Command Packet: Invalid Command packet {}",
                         first_12_bytes_hex);
    }
    return fmt::format(
        "RTP Command Packet: Command:{} Protocol:{} SSRC:{} Count: {} "
        "CK0:{} CK1:{} CK2:{}",
        get_command(), get_protocol_version(), get_sender_ssrc(), get_count(),
        get_ck0(), get_ck1(), get_ck2());
  }

  packet_t as_send_packet() const { return packet_t(data, 36); }
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

// allow fmt to format rtpmidid::packet_midi_t
template <> struct fmt::formatter<rtpmidid::packet_midi_t> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::packet_midi_t &p, FormatContext &ctx) {
    return format_to(ctx.out(), "{}", p.to_string());
  }
};

// allow fmt to format rtpmidid::packet_command_t
template <> struct fmt::formatter<rtpmidid::packet_command_t> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const rtpmidid::packet_command_t &p, FormatContext &ctx) {
    return format_to(ctx.out(), "{}", p.to_string());
  }
};

namespace std {
static inline rtpmidid::midi_event_list_t::iterator_t
begin(rtpmidid::midi_event_list_t &lst) {
  return lst.begin();
};
static inline rtpmidid::midi_event_list_t::iterator_t
end(rtpmidid::midi_event_list_t &lst) {
  return lst.end();
}
} // namespace std
