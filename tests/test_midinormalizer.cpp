/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "rtpmidid/packet.hpp"
#include "test_case.hpp"
#include "test_utils.hpp"
#include <midi_normalizer.hpp>
#include <vector>

void test_midinormalizer_note_on_off() {
  rtpmididns::midi_normalizer_t normalizer;

  normalizer.parse_midi_byte(0x90, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 0);
  });
  normalizer.parse_midi_byte(0x64, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 0);
  });
  normalizer.parse_midi_byte(0x7F, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x90);
    ASSERT_EQUAL(packet.get_uint8(1), 0x64);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
  });

  normalizer.parse_midi_byte(0x80, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 0);
  });
  normalizer.parse_midi_byte(0x64, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 0);
  });
  normalizer.parse_midi_byte(0x7F, [](const rtpmidid::packet_t &packet) {
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x80);
    ASSERT_EQUAL(packet.get_uint8(1), 0x64);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
  });
}

void test_midinormalizer_sysex() {
  rtpmididns::midi_normalizer_t normalizer;

  rtpmidid::packet_managed_t<1024> packet;
  packet.set_uint8(0, 0xF0);
  packet.set_uint8(1, 0xF7);

  // Minimal packet
  normalizer.normalize_stream(packet.slice(0, 2),
                              [](const rtpmidid::packet_t &packet) {
                                ASSERT_EQUAL(packet.get_size(), 2);
                                ASSERT_EQUAL(packet.get_uint8(0), 0xF0);
                                ASSERT_EQUAL(packet.get_uint8(1), 0xF7);
                              });

  // A longer packet
  packet.set_uint8(0, 0xF0);
  for (int i = 1; i < 128; i++) {
    packet.set_uint8(i, i & 0x7F);
  }
  packet.set_uint8(128, 0xF7);

  normalizer.normalize_stream(packet.slice(0, 128),
                              [](const rtpmidid::packet_t &packet) {
                                ASSERT_EQUAL(packet.get_size(), 129);
                                ASSERT_EQUAL(packet.get_uint8(0), 0xF0);
                                for (int i = 1; i < 128; i++) {
                                  ASSERT_EQUAL(packet.get_uint8(i), i & 0x7F);
                                }
                                ASSERT_EQUAL(packet.get_uint8(128), 0xF7);
                              });
}

void test_midinormalizer_long_packet_into_several_check_packet(
    int ncall, const rtpmidid::packet_t &packet) {
  switch (ncall) {
  case 0:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x90);
    ASSERT_EQUAL(packet.get_uint8(1), 0x64);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 1:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x90);
    ASSERT_EQUAL(packet.get_uint8(1), 0x65);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 2:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x90);
    ASSERT_EQUAL(packet.get_uint8(1), 0x66);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 3:
    ASSERT_EQUAL(packet.get_size(), 6);
    ASSERT_EQUAL(packet.get_uint8(0), 0xF0);
    ASSERT_EQUAL(packet.get_uint8(1), 0x01);
    ASSERT_EQUAL(packet.get_uint8(2), 0x02);
    ASSERT_EQUAL(packet.get_uint8(3), 0x03);
    ASSERT_EQUAL(packet.get_uint8(4), 0x04);
    ASSERT_EQUAL(packet.get_uint8(5), 0xF7);
    break;
  case 4:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x80);
    ASSERT_EQUAL(packet.get_uint8(1), 0x64);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 5:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x80);
    ASSERT_EQUAL(packet.get_uint8(1), 0x65);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 6:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0x80);
    ASSERT_EQUAL(packet.get_uint8(1), 0x66);
    ASSERT_EQUAL(packet.get_uint8(2), 0x7F);
    break;
  case 7:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0xB0);
    ASSERT_EQUAL(packet.get_uint8(1), 0x01);
    ASSERT_EQUAL(packet.get_uint8(2), 0x02);
    break;
  case 8:
    ASSERT_EQUAL(packet.get_size(), 3);
    ASSERT_EQUAL(packet.get_uint8(0), 0xB0);
    ASSERT_EQUAL(packet.get_uint8(1), 0x03);
    ASSERT_EQUAL(packet.get_uint8(2), 0x04);
    break;
  default:
    ASSERT_TRUE(false);
  }
}

void test_midinormalizer_long_packet_into_several() {
  rtpmididns::midi_normalizer_t normalizer;

  std::vector<uint8_t> data{
      0x90, 0x64, 0x7F, // Note on
      0x90, 0x65, 0x7F, // Note on
      0x90, 0x66, 0x7F, // Note on
      // now a sysex
      0xF0, 0x01, 0x02, 0x03, 0x04, 0xF7,
      // note offs
      0x80, 0x64, 0x7F, // Note off
      0x80, 0x65, 0x7F, // Note off
      0x80, 0x66, 0x7F, // Note off
      // now some CC changes
      0xB0, 0x01, 0x02, // CC change
      0xB0, 0x03, 0x04, // CC change
  };
  rtpmidid::packet_t packet(data);

  int ncall = 0;
  normalizer.normalize_stream(packet, [&](const rtpmidid::packet_t &packet) {
    test_midinormalizer_long_packet_into_several_check_packet(ncall, packet);
    ncall++;
  });

  ASSERT_EQUAL(ncall, 9);

  // Now split the packet into n parts, same results
  INFO("Now split the packet into n parts, same results");
  ncall = 0;
  int nparts = 4;
  int nbytes = data.size();
  int length = nbytes / nparts;
  int i;
  for (i = 0; i < nbytes; i += length) {
    normalizer.normalize_stream(
        packet.slice(i, std::min(length, nbytes - i)),
        [&](const rtpmidid::packet_t &packet) {
          test_midinormalizer_long_packet_into_several_check_packet(ncall,
                                                                    packet);
          ncall++;
        });
  }

  ASSERT_EQUAL(ncall, 9);
  ASSERT_GTE(i, nbytes);
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_midinormalizer_note_on_off),
                       TEST(test_midinormalizer_sysex),
                       TEST(test_midinormalizer_long_packet_into_several)};

  testcase.run(argc, argv);
  return testcase.exit_code();
}
