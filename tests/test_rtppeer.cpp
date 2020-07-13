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

#include "../tests/test_utils.hpp"
#include "./test_case.hpp"
#include <memory>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/parse_buffer.hpp>
#include <rtpmidid/rtppeer.hpp>

auto CONNECT_MSG = hex_to_bin("FF FF 'IN'"
                              "0000 0002"
                              "'FA57' 'BEEF'"
                              "'testing' 00");
auto DISCONNECT_MSG = hex_to_bin("FF FF 'BY'"
                                 "0000 0002"
                                 "'FA57' 'BEEF'");

void test_connect_disconnect() {
  rtpmidid::rtppeer peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  rtpmidid::rtppeer::status_e connected =
      rtpmidid::rtppeer::status_e::NOT_CONNECTED;
  peer.connected_event.connect(
      [&connected](const std::string &name, rtpmidid::rtppeer::status_e st) {
        DEBUG("Connected from {}, status: {}", name, st);
        connected = st;
      });
  peer.disconnect_event.connect([&connected](auto reason) {
    DEBUG("Disconnected. Reason: {}", reason);
    connected = rtpmidid::rtppeer::status_e::NOT_CONNECTED;
  });
  peer.send_event.connect(
      [](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port) {
        DEBUG("Send to {}:",
              port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
        data.print_hex();
      });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);

  // Control connect
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONTROL_CONNECTED);

  // MIDI connect. Same all.
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONNECTED);

  peer.data_ready(*DISCONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, false);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_connect_disconnect_reverse_order() {
  rtpmidid::rtppeer peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  auto connected = rtpmidid::rtppeer::status_e::NOT_CONNECTED;
  peer.connected_event.connect(
      [&connected](const std::string &_name, rtpmidid::rtppeer::status_e st) {
        connected = st;
      });
  peer.disconnect_event.connect([&connected](auto reason) {
    connected = rtpmidid::rtppeer::status_e::NOT_CONNECTED;
  });
  peer.send_event.connect(
      [](const rtpmidid::parse_buffer_t &data, rtpmidid::rtppeer::port_e port) {
        DEBUG("Send to {}:",
              port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
        data.print_hex();
      });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);

  // Normally should be control first, but network is a B*** and sometimes is in
  // the other order Also I'm liberal on clients as they should not send the
  // midi conenct until they get the control connect. But I do it myself.
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::MIDI_CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);

  // Control connect. Same all.
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(*DISCONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_send_some_midi() {
  rtpmidid::rtppeer peer("test");

  bool sent_midi = false;
  peer.send_event.connect([&peer,
                           &sent_midi](const rtpmidid::parse_buffer_t &data,
                                       rtpmidid::rtppeer::port_e port) {
    if (peer.is_connected()) {
      data.print_hex();

      auto midi_data =
          rtpmidid::parse_buffer_t(data.start + 12, data.capacity() - 12);
      ASSERT_TRUE(midi_data.compare(*hex_to_bin("07 90 64 7F 68 7F 71 7F")));
      sent_midi = true;
    }
  });

  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.send_midi(*hex_to_bin("90 64 7F 68 7F 71 7F"));

  ASSERT_TRUE(sent_midi);
}

void test_recv_some_midi() {
  rtpmidid::rtppeer peer("test");

  bool got_midi = false;

  // This will be called when I get some midi data.
  peer.midi_event.connect([&peer, &got_midi](rtpmidid::parse_buffer_t &data) {
    ASSERT_TRUE(peer.is_connected());
    ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);
    data.print_hex(true);
    got_midi = true;
    ASSERT_TRUE(data.compare(*hex_to_bin("90 64 7F 68 7F 71 7F")));
  });

  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.data_ready(
      *hex_to_bin(
          "[1000 0001] [0110 0001] 'SQ'"
          "00 00 00 00"
          "'BEEF'"
          "07 90 64 7F 68 7F 71 7F" // No Journal, 7 bytes, Three note ons
          ),
      rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_TRUE(got_midi);
}

void test_journal() {
  rtpmidid::rtppeer peer("test");

  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  peer.data_ready(*CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

  // I send seq 0, no notes, just to set the sequence
  peer.data_ready(*hex_to_bin("[1000 0001] [0110 0001] "
                              "00 00"       // Sequence 0
                              "00 00 00 00" // Timestamp
                              "'BEEF'"      // SSRC
                              "[0000 0000]" // No MIDI data, empty packet
                              ),
                  rtpmidid::rtppeer::MIDI_PORT);

  DEBUG("Send journal");
  peer.data_ready(*hex_to_bin("[1000 0001] [0110 0001] "
                              "00 02"       // Sequence 2 -- where is? Lost!
                              "00 00 00 00" // Timestamp
                              "'BEEF'"      // SSRC
                              "[0100 "      // Only send journal
                              " 0000]"      // No midi notes
                              // journal here
                              "[1010"  // SyAh
                              " 0001]" // TOTCHAN
                              "00 00"  // SEQNO
                              ),
                  rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
      TEST(test_connect_disconnect_reverse_order),
      TEST(test_send_some_midi),
      TEST(test_recv_some_midi),
      TEST(test_journal),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
