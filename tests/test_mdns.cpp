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
#include "../src/logger.hpp"
#include "./test_case.hpp"
#include "./test_utils.hpp"

auto packet_dns = hex_to_bin(
  "00 00 84 00" // DNS header and flags
  "00 00 00 01" // 1 question
  "00 00 00 01" // 1 answer
  // question, pos 12
  "0B '_apple-midi' 04 '_udp' 05 'local' 00"
  "00 0C" // PTR
  "00 01" // Class IN
  // answer, pos 40
  "C0 0C" // label pointer to question label
  "00 0C" // PTR
  "00 01" // Class IN
  "00 00 10 00" // TTL
  "00 0C" // data length, 13 bytes
  "0B 'test-server' C0 0C" // pos 52
);

void test_label_simple(){
  auto buffer = *packet_dns;
  buffer.print_hex(true);
  uint8_t label[120];
  auto label_buffer = rtpmidid::parse_buffer_t(label, sizeof(label));

  // label position
  buffer.position += 12;
  rtpmidid::read_label(buffer, label_buffer);

  ASSERT_EQUAL(strncmp((char*)label, "_apple-midi._udp.local", sizeof(label)), 0);
}

void test_label_w_pointer(){
  auto buffer = *packet_dns;
  uint8_t label[120];
  auto label_buffer = rtpmidid::parse_buffer_t(label, sizeof(label));

  // label with pointer position
  buffer.position = buffer.start + 40;
  rtpmidid::read_label(buffer, label_buffer);
  DEBUG("{}", (char*)&label);
  ASSERT_EQUAL(strncmp((char*)label, "_apple-midi._udp.local", sizeof(label)), 0);

  buffer.position = buffer.start + 52;
  label_buffer.position = label_buffer.start;
  rtpmidid::read_label(buffer, label_buffer);
  DEBUG("{}", (char*)&label);
  ASSERT_EQUAL(strncmp((char*)label, "test-server._apple-midi._udp.local", sizeof(label)), 0);
}


int main(){
  test_case_t tests{
    TEST(test_label_simple),
    TEST(test_label_w_pointer)
  };
  tests.run();

  return tests.exit_code();
}
