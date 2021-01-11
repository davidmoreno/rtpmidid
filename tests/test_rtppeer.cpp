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
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
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
  peer.send_event.connect([](const rtpmidid::io_bytes_reader &data,
                             rtpmidid::rtppeer::port_e port) {
    DEBUG("Send to {}:",
          port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
    data.print_hex();
  });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);

  // Control connect
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONTROL_CONNECTED);

  // MIDI connect. Same all.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONNECTED);

  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

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
  peer.send_event.connect([](const rtpmidid::io_bytes_reader &data,
                             rtpmidid::rtppeer::port_e port) {
    DEBUG("Send to {}:",
          port == rtpmidid::rtppeer::CONTROL_PORT ? "Control" : "MIDI");
    data.print_hex();
  });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);

  // Normally should be control first, but network is a B*** and sometimes is in
  // the other order Also I'm liberal on clients as they should not send the
  // midi conenct until they get the control connect. But I do it myself.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::MIDI_CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);

  // Control connect. Same all.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

  ASSERT_EQUAL(connected, rtpmidid::rtppeer::status_e::NOT_CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_send_short_midi() {
  rtpmidid::rtppeer peer("test");

  bool sent_midi = false;
  peer.send_event.connect([&peer,
                           &sent_midi](const rtpmidid::io_bytes_reader &data,
                                       rtpmidid::rtppeer::port_e port) {
    if (peer.is_connected()) {
      data.print_hex();

      auto midi_buffer =
          rtpmidid::io_bytes_reader(data.start + 12, data.size() - 12);
      ASSERT_TRUE(midi_buffer.compare(hex_to_bin("07 90 64 7F 68 7F 71 7F")));
      sent_midi = true;
    }
  });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.send_midi(hex_to_bin("90 64 7F 68 7F 71 7F"));

  ASSERT_TRUE(sent_midi);
}

void test_send_long_midi() {
    rtpmidid::rtppeer peer("test");

    bool sent_midi = false;
    peer.send_event.connect([&peer,
                                    &sent_midi](const rtpmidid::io_bytes_reader &data,
                                                rtpmidid::rtppeer::port_e port) {
        if (peer.is_connected()) {
            data.print_hex();

            auto midi_buffer =
                    rtpmidid::io_bytes_reader(data.start + 12, data.size() - 12);
            ASSERT_TRUE(midi_buffer.compare(hex_to_bin("80 11 F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7")));
            sent_midi = true;
        }
    });

    peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
    peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

    peer.send_midi(hex_to_bin("F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7"));

    ASSERT_TRUE(sent_midi);
}

void test_recv_some_midi() {
  rtpmidid::rtppeer peer("test");

  bool got_midi = false;

  // This will be called when I get some midi data.
  peer.midi_event.connect(
      [&peer, &got_midi](const rtpmidid::io_bytes_reader &data) {
        ASSERT_TRUE(peer.is_connected());
        ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);
        data.print_hex(true);
        got_midi = true;
        ASSERT_TRUE(data.compare(hex_to_bin("90 64 7F 68 7F 71 7F")));
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.data_ready(
      hex_to_bin(
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

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);

  rtpmidid::io_bytes_writer_static<16> midi_io;
  rtpmidid::io_bytes_writer_static<256> network_io;

  peer.midi_event.connect([&midi_io](const rtpmidid::io_bytes &pb) {
    midi_io.copy_from(pb.start, pb.size());
  });
  peer.send_event.connect([&network_io](const rtpmidid::io_bytes &pb,
                                        rtpmidid::rtppeer::port_e port) {
    DEBUG("Write to network: {} bytes", pb.size());
    network_io.copy_from(pb.start, pb.size());
  });

  // I send seq 0, no notes, just to set the sequence
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"       // Sequence 0
                             "00 00 00 00" // Timestamp
                             "'BEEF'"      // SSRC
                             "[0000 0000]" // No MIDI data, empty packet
                             ),
                  rtpmidid::rtppeer::MIDI_PORT);

  DEBUG("Send 2nd packet. First lost. Note On.");
  peer.data_ready(
      hex_to_bin("[1000 0001] [0110 0001] "
                 "00 02"       // Sequence 2 -- where is seq 1? Lost!
                 "00 00 00 10" // Timestamp
                 "'BEEF'"      // SSRC
                 "[0100 "      // Only send journal
                 " 0000]"      // No midi notes
                 // journal here
                 "[1010"  // SyAh
                 " 0001]" // TOTCHAN
                 "00 02"  // SEQNO
                 // chan1 journal
                 "[0 000 0 000]"      // S0, chan0, H0, len MSB
                 "00"                 // length LSB
                 "[0 0 0 0  1 0 0 0]" // Included chapters, only N - Notes
                 // chan1 N journal
                 "[0 000 0001] " // S0, 1 noteon
                 "[1111 0000]"   //  LOW 15, HIGH 0 => 0 noteoffs.
                 // LOW is floor(minnote / 8), HIGH = ceil(minnote / 8), so
                 // length is HIGH - LOW + 1.
                 "48 ff" // C4 vel 127. S0 Y1 - Y1 means play, Y0 skip
                 ),
      rtpmidid::rtppeer::MIDI_PORT);

  DEBUG("Send 4th packet. Second lost too. Note Off.");

  // Assume Ack, and lost packet so send the note off some time later
  peer.data_ready(
      hex_to_bin("[1000 0001] [0110 0001] "
                 "00 04"       // Sequence 4 -- where is seq 3? Lost too!
                 "00 00 00 20" // Timestamp
                 "'BEEF'"      // SSRC
                 "[0100 "      // Only send journal
                 " 0000]"      // No midi notes
                 // journal here
                 "[1010"  // SyAh
                 " 0001]" // TOTCHAN
                 "00 04"  // SEQNO
                 // chan1 journal
                 "[0 000 0 000]"      // S0, chan0, H0, len MSB
                 "00"                 // length LSB
                 "[0 0 0 0  1 0 0 0]" // Included chapters, only N - Notes
                 // chan1 N journal
                 "[0 000 0000] " // S0, 0 noteon,
                 "[1001 1001]"   //  LOW=HIGH = 6. C4, 78 / 8 = 9.75
                 // HIGH - LOW + 1.
                 "[1000 0000]" // C4 off
                 ),
      rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);

  DEBUG("MIDI DATA. {} bytes", midi_io.pos());
  midi_io.print_hex();

  DEBUG("NETWORK DATA, {} bytes", network_io.pos());
  network_io.print_hex();
  // There should be some data at network_buffer
  ASSERT_NOT_EQUAL(network_io.pos(), 0);

  ASSERT_EQUAL(midi_io.data[0], 0x90);
  ASSERT_EQUAL(midi_io.data[1], 0x48);
  ASSERT_EQUAL(midi_io.data[2], 0x7f);

  ASSERT_EQUAL(midi_io.data[3], 0x80);
  ASSERT_EQUAL(midi_io.data[4], 0x48);
  ASSERT_EQUAL(midi_io.data[5], 0x0);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
      TEST(test_connect_disconnect_reverse_order),
      TEST(test_send_short_midi),
      TEST(test_send_long_midi),
      TEST(test_recv_some_midi),
      TEST(test_journal),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
