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
#include <cstdint>
#include "../src/netutils.hpp"
#include "./testcase.hpp"

uint8_t packet_res[] = {
  0x00, 0x00, 0x84, 0x00 , 0x00, 0x00, 0x00, 0x01 , 0x00, 0x00, 0x00, 0x00 , 0x0B, 0x5F, 0x61, 0x70, // 16
  0x70, 0x6C, 0x65, 0x2D , 0x6D, 0x69, 0x64, 0x69 , 0x04, 0x5F, 0x75, 0x64 , 0x70, 0x05, 0x6C, 0x6F, // 32
  0x63, 0x61, 0x6C, 0x00 , 0x00, 0x0C, 0x00, 0x01 , 0x00, 0x00, 0x00, 0x78 , 0x00, 0x0D, 0x0A, 0x75, // 48
  0x63, 0x75, 0x62, 0x65 , 0x2D, 0x77, 0x69, 0x6E , 0x65, 0xC0, 0x0B
};

bool test_label_simple(){
  auto buffer = rtpmidid::parse_buffer_t(packet_res, sizeof(packet_res));
  uint8_t label[120];
  auto label_buffer = rtpmidid::parse_buffer_t(label, sizeof(label));

  // label position
  buffer.position += 12;
  rtpmidid::read_label(buffer, label_buffer);

  if (strncmp((char*)label, "_apple-midi._udp.local", sizeof(label)) != 0){
    fprintf(stderr, "Label does not match: %s\n", label);
    return false;
  }
  return true;
}

bool test_label_w_pointer(){
  auto buffer = rtpmidid::parse_buffer_t(packet_res, sizeof(packet_res));
  uint8_t label[120];
  auto label_buffer = rtpmidid::parse_buffer_t(label, sizeof(label));

  // label with pointer position
  buffer.position += 46;
  rtpmidid::read_label(buffer, label_buffer);

  if (strncmp((char*)label, "_apple-midi._udp.local", sizeof(label)) != 0){
    fprintf(stderr, "Label with pointer does not match: %s\n", label);
    return false;
  }
  return true;
}


int main(){
  TestCase tests{
    TEST(test_label_simple),
    TEST(test_label_w_pointer)
  };
  tests.run();

  return tests.exit_code();
}
