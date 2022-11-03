/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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
#include <algorithm>
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
  peer.send_event.connect(
      [&peer, &sent_midi](const rtpmidid::io_bytes_reader &data,
                          rtpmidid::rtppeer::port_e port) {
        if (peer.is_connected()) {
          data.print_hex();

          auto midi_buffer =
              rtpmidid::io_bytes_reader(data.start + 12, data.size() - 12);
          ASSERT_TRUE(midi_buffer.compare(hex_to_bin(
              "80 11 F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7")));
          sent_midi = true;
        }
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.send_midi(
      hex_to_bin("F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7"));

  ASSERT_TRUE(sent_midi);
}

void test_recv_some_midi() {
  rtpmidid::rtppeer peer("test");

  int got_midi_nr = 0;

  // This will be called when I get some midi data.
  peer.midi_event.connect(
      [&peer, &got_midi_nr](const rtpmidid::io_bytes_reader &data) {
        ASSERT_TRUE(peer.is_connected());
        ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);
        data.print_hex(true);
        if (data.compare(hex_to_bin("90 64 7F"))) {
          ASSERT_EQUAL(got_midi_nr, 0);
          got_midi_nr++;
        } else if (data.compare(hex_to_bin("90 7F 71"))) {
          ASSERT_EQUAL(got_midi_nr, 1);
          got_midi_nr++;

        } else if (data.compare(hex_to_bin("F8"))) {
          ASSERT_EQUAL(got_midi_nr, 2);
          got_midi_nr++;
        }
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] 'SQ'"
                             "00 00 00 00"
                             "'BEEF'"
                             "0B"                               // No Journal, 11 bytes
                             "90 64 7F 00 90 7F 71 80 80 00 F8" // Two note ons and one clock
                             ),                                 // Delta times zero (2 different encodings)
                  rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_EQUAL(got_midi_nr, 3);
}

void test_recv_midi_with_running_status() {
  rtpmidid::rtppeer peer("test");

  int got_midi_nr = 0;

  // This will be called when I get some midi data.
  peer.midi_event.connect(
      [&peer, &got_midi_nr](const rtpmidid::io_bytes_reader &data) {
        ASSERT_TRUE(peer.is_connected());
        ASSERT_EQUAL(peer.status, rtpmidid::rtppeer::status_e::CONNECTED);
        data.print_hex(true);
        if (data.compare(hex_to_bin("BF 6D 24"))) {
          ASSERT_EQUAL(got_midi_nr, 0);
          got_midi_nr++;
        } else if (data.compare(hex_to_bin("BF 37 01"))) {
          ASSERT_EQUAL(got_midi_nr, 1);
          got_midi_nr++;
        } else if (data.compare(hex_to_bin("BF 6D 20"))) {
          ASSERT_EQUAL(got_midi_nr, 2);
          got_midi_nr++;
        }
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer::MIDI_PORT);

  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] 'SQ'"
                             "00 00 00 00"
                             "'BEEF'"
                             "09"                          // No Journal, 9 bytes
                             "BF 6D 24 00 37 01 00 6D 20"  // 3 CC commands on ch15 (running status)
                             ),                            // Delta times are zero
                  rtpmidid::rtppeer::MIDI_PORT);

  ASSERT_EQUAL(got_midi_nr, 3);
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

void test_send_large_sysex(void) {
  const auto sysex = hex_to_bin(
      "F0 " // this was not in the report.. maybe a bug? if there everything
            // makes sense
      // Bunch of empty sysex.
      "F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 "
      "F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 "
      "F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F7"
      "00 F0"
      "44 01 47 57 2D "
      "00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 "
      "00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 "
      "12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 "
      "0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 "
      "18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 "
      "40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 "
      "17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 "
      "00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 "
      "2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 "
      "3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 "
      "44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F "
      "34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C "
      "48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 "
      "47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F "
      "2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 "
      "01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 "
      "2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 "
      "60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 "
      "44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 "
      "00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 "
      "04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 "
      "04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D "
      "33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 "
      "F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 "
      "00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 "
      "00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 "
      "F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E "
      "1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E "
      "0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 "
      "01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 "
      "1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 "
      "31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 "
      "57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B "
      "69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 "
      "09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D "
      "00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 "
      "00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 "
      "12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 "
      "0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 "
      "18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 "
      "40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 "
      "17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 "
      "00 F7 44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 "
      "2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 44 01 47 57 2D 00 00 0D 33 17 00 "
      "3E 0C 48 31 01 09 44 12 04 40 00 2E 1F 34 1F 2B 69 60 00 04 18 F0 00 F7 "
      "44 01 47 57 2D 00 00 0D 33 17 00 3E 0C 48 31 01 09 44 12 04 40 00 2E 1F "
      "34 1F 2B 69 60 00 04 18 F7 "
      // and more empty packets
      "00 F0 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 "
      "F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 "
      "F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 "
      "F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 "
      "F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 F7 F0 00 "
      "F7 F0 00 F7 F7");

  rtpmidid::rtppeer sender("sender");
  rtpmidid::rtppeer receiver("receiver");

  sender.send_event.connect([&receiver](const rtpmidid::io_bytes_reader &data,
                                        rtpmidid::rtppeer::port_e port) {
    rtpmidid::io_bytes_reader datar(data);
    DEBUG("Write {} bytes to receiver data_ready", data.size());
    receiver.data_ready(std::move(datar), port);
  });

  receiver.send_event.connect([&sender](const rtpmidid::io_bytes_reader &data,
                                        rtpmidid::rtppeer::port_e port) {
    rtpmidid::io_bytes_reader datar(data);
    DEBUG("Write {} bytes to sender data_ready", data.size());
    sender.data_ready(std::move(datar), port);
  });

  bool got_midi = false;

  receiver.midi_event.connect(
      [&got_midi](const rtpmidid::io_bytes_reader &midi) {
        INFO("Got MIDI data, size: {}", midi.size());
        // midi.print_hex();
        ASSERT_EQUAL(*midi.position, 0xF0);
        ASSERT_EQUAL(*(midi.end - 1), 0xF7);
        INFO("Got MIDI data, size: {}", midi.size());
        ASSERT_EQUAL(midi.size(), 1026);

        got_midi = true;
      });

  sender.connect_to(rtpmidid::rtppeer::CONTROL_PORT);
  sender.connect_to(rtpmidid::rtppeer::MIDI_PORT);

  sender.send_midi(sysex);

  ASSERT_TRUE(got_midi);
}

void test_segmented_sysex(void) {
  const auto segmented_sysex1 = hex_to_bin("F0 01 02 03 04 F0");
  const auto segmented_sysex2 = hex_to_bin("F7 05 06 07 08 F7");
  const auto cancel_sysex = hex_to_bin("F7 F4");
  const auto sysex = hex_to_bin("F0 01 02 03 0405 06 07 08 F7");

  rtpmidid::rtppeer sender("sender");
  rtpmidid::rtppeer receiver("receiver");

  sender.send_event.connect([&receiver](const rtpmidid::io_bytes_reader &data,
                                        rtpmidid::rtppeer::port_e port) {
    rtpmidid::io_bytes_reader datar(data);
    DEBUG("Write {} bytes to receiver data_ready", data.size());
    receiver.data_ready(std::move(datar), port);
  });

  receiver.send_event.connect([&sender](const rtpmidid::io_bytes_reader &data,
                                        rtpmidid::rtppeer::port_e port) {
    rtpmidid::io_bytes_reader datar(data);
    DEBUG("Write {} bytes to sender data_ready", data.size());
    sender.data_ready(std::move(datar), port);
  });
  bool got_data = false;
  receiver.midi_event.connect(
      [&got_data, &sysex](const rtpmidid::io_bytes_reader &midi) {
        INFO("Got MIDI data");
        // midi.print_hex();
        DEBUG("Got {} bytes, need {} bytes", midi.size(), sysex.size());
        ASSERT_EQUAL(midi.size(), sysex.size());
        ASSERT_EQUAL(memcmp(midi.start, sysex.start, midi.size()), 0);

        got_data = true;
      });

  sender.connect_to(rtpmidid::rtppeer::CONTROL_PORT);
  sender.connect_to(rtpmidid::rtppeer::MIDI_PORT);

  DEBUG("Send p1");
  sender.send_midi(segmented_sysex1);
  DEBUG("Send cancel");
  sender.send_midi(cancel_sysex);

  DEBUG("Send p1");
  sender.send_midi(segmented_sysex1);
  DEBUG("Send p2");
  sender.send_midi(segmented_sysex2);

  ASSERT_TRUE(got_data);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_connect_disconnect),
      TEST(test_connect_disconnect_reverse_order),
      TEST(test_send_short_midi),
      TEST(test_send_long_midi),
      TEST(test_recv_some_midi),
      TEST(test_recv_midi_with_running_status),
      TEST(test_journal),
      TEST(test_send_large_sysex),
      TEST(test_segmented_sysex),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
