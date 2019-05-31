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

#include <memory>
#include "./testcase.hpp"
#include "../src/rtppeer.hpp"
#include "../src/netutils.hpp"
#include "../tests/test_utils.hpp"

void test_connect_disconnect(){
  rtpmidid::rtppeer peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  bool connected = false;
  peer.on_connect([&connected](const std::string &_name){ connected = true; });
  peer.on_disconnect([&connected](){ connected = false; });
  peer.on_send([](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port){
      DEBUG("Send to {}:", port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
      data.print_hex();
  });

  ASSERT_EQUAL(connected, false);

  // Control connect
  peer.data_ready(*hex_to_bin(
    "FF FF 'IN'"
    "0000 0002"
    "'FA57' 'BEEF'"
    "'testing' 00"
  ), rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);

  // MIDI connect. Same all.
  peer.data_ready(*hex_to_bin(
    "FF FF 'IN'"
    "0000 0002"
    "'FA57' 'BEEF'"
    "'testing' 00"
  ), rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, true);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(*hex_to_bin(
    "FF FF 'BY'"
    "0000 0002"
    "'FA57' 'BEEF'"
  ), rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);
}

int main(void){
  TestCase testcase{
    TEST(test_connect_disconnect),
  };

  testcase.run();

  return testcase.exit_code();
}
