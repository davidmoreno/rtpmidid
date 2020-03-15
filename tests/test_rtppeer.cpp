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
#include "./test_case.hpp"
#include "../src/rtppeer.hpp"
#include "../src/netutils.hpp"
#include "../tests/test_utils.hpp"

auto connect_msg = hex_to_bin(
  "FF FF 'IN'"
  "0000 0002"
  "'FA57' 'BEEF'"
  "'testing' 00"
);
auto disconnect_msg = hex_to_bin(
  "FF FF 'BY'"
  "0000 0002"
  "'FA57' 'BEEF'"
);

void test_connect_disconnect(){
  rtpmidid::rtppeer peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  bool connected = false;
  peer.connected_event.connect([&connected](const std::string &_name, rtpmidid::rtppeer::status_e st){ 
    connected = true; 
  });
  peer.disconnect_event.connect([&connected](auto reason){ connected = false; });
  peer.send_event.connect([](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port){
      DEBUG("Send to {}:", port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
      data.print_hex();
  });

  ASSERT_EQUAL(connected, false);

  // Control connect
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);

  // MIDI connect. Same all.
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, true);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(*disconnect_msg, rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_connect_disconnect_reverse_order(){
  rtpmidid::rtppeer peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  bool connected = false;
  peer.connected_event.connect([&connected](const std::string &_name, rtpmidid::rtppeer::status_e st){ 
    connected = true; 
  });
  peer.disconnect_event.connect([&connected](auto reason){ connected = false; });
  peer.send_event.connect([](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port){
      DEBUG("Send to {}:", port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
      data.print_hex();
  });

  ASSERT_EQUAL(connected, false);

  // Normally should be control first, but network is a B*** and sometimes is in the other order
  // Also I'm liberal on clients as they should not send the midi conenct until they get the
  // control connect. But I do it myself.
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);

  // Control connect. Same all.
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, true);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(*disconnect_msg, rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_send_some_midi(){
  rtpmidid::rtppeer peer("test");
  bool connected = false;
  peer.connected_event.connect([&connected](const std::string &_name, rtpmidid::rtppeer::status_e st){ connected = true; });
  peer.disconnect_event.connect([&connected](auto reason){ connected = false; });

  bool sent_midi = false;
  peer.send_event.connect([&connected, &sent_midi](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port){
    if (connected){
      data.print_hex();

      auto midi_data = rtpmidid::parse_buffer_t(data.start + 12, data.capacity() - 12);
      ASSERT_TRUE(midi_data.compare(*hex_to_bin(
        "07 90 64 7F 68 7F 71 7F"
      )));
      sent_midi = true;
    }
  });

  peer.data_ready(*connect_msg, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::MIDI_PORT);

  peer.send_midi(*hex_to_bin(
    "90 64 7F 68 7F 71 7F"
  ));

  ASSERT_TRUE(sent_midi);
}

void test_recv_some_midi(){
  rtpmidid::rtppeer peer("test");
  bool connected = false;
  peer.connected_event.connect([&connected](const std::string &_name, rtpmidid::rtppeer::status_e st){ 
    connected = true; 
  });
  peer.disconnect_event.connect([&connected](auto reason){ connected = false; });

  bool got_midi = false;

  peer.send_event.connect([](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port){
    // Nothing
  });

  // This will be called when I get some midi data.
  peer.midi_event.connect([&connected, &got_midi](rtpmidid::parse_buffer_t &data){
    ASSERT_EQUAL(connected, true);
    data.print_hex(true);
    got_midi = true;
    ASSERT_TRUE(data.compare(*hex_to_bin(
      "90 64 7F 68 7F 71 7F"
    )));
  });

  peer.data_ready(*connect_msg, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(*connect_msg, rtpmidid::rtppeer::MIDI_PORT);

  peer.data_ready(*hex_to_bin(
    "81 61 'SQ'"
    "00 00 00 00"
    "'BEEF'"
    "07 90 64 7F 68 7F 71 7F" // No Journal, 7 bytes, Three note ons
  ), rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_TRUE(got_midi);
}

int main(void){
  test_case_t testcase{
    TEST(test_connect_disconnect),
    TEST(test_connect_disconnect_reverse_order),
    TEST(test_send_some_midi),
    TEST(test_recv_some_midi),
  };

  testcase.run();

  return testcase.exit_code();
}
