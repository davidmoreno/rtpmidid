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
  rtpmidid::rtppeer_t peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  rtpmidid::rtppeer_t::status_e connected =
      rtpmidid::rtppeer_t::status_e::NOT_CONNECTED;
  auto connected_event_c1 = peer.status_change_event.connect(
      [&connected](rtpmidid::rtppeer_t::status_e st) {
        DEBUG("Status change: {}", st);
        connected = st;
      });
  auto connected_event_c3 =
      peer.send_event.connect([](const rtpmidid::io_bytes_reader &data,
                                 rtpmidid::rtppeer_t::port_e port) {
        DEBUG("Send to {}:",
              port == rtpmidid::rtppeer_t::CONTROL_PORT ? "Control" : "MIDI");
        data.print_hex();
      });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::NOT_CONNECTED);

  // Control connect
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::CONTROL_CONNECTED);

  // MIDI connect. Same all.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::CONNECTED);

  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_GT(connected, rtpmidid::rtppeer_t::DISCONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_connect_disconnect_reverse_order() {
  rtpmidid::rtppeer_t peer("test");

  ASSERT_EQUAL(peer.is_connected(), false);

  auto connected = rtpmidid::rtppeer_t::status_e::NOT_CONNECTED;
  auto connected_event_c1 = peer.status_change_event.connect(
      [&connected](rtpmidid::rtppeer_t::status_e st) {
        connected = st;
        INFO("Got status {}", st);
      });
  auto send_event_c1 =
      peer.send_event.connect([](const rtpmidid::io_bytes_reader &data,
                                 rtpmidid::rtppeer_t::port_e port) {
        DEBUG("Send to {}:",
              port == rtpmidid::rtppeer_t::CONTROL_PORT ? "Control" : "MIDI");
        data.print_hex();
      });

  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::NOT_CONNECTED);

  // Normally should be control first, but network is a B*** and sometimes is in
  // the other order Also I'm liberal on clients as they should not send the
  // midi conenct until they get the control connect. But I do it myself.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::MIDI_CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);

  // Control connect. Same all.
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  ASSERT_EQUAL(connected, rtpmidid::rtppeer_t::status_e::CONNECTED);
  ASSERT_EQUAL(peer.is_connected(), true);

  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::MIDI_CONNECTED);
  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::NOT_CONNECTED);

  ASSERT_GTE(connected, rtpmidid::rtppeer_t::status_e::DISCONNECTED);
  ASSERT_EQUAL(peer.is_connected(), false);
}

void test_send_short_midi() {
  rtpmidid::rtppeer_t peer("test");

  bool sent_midi = false;
  auto send_event_c1 = peer.send_event.connect(
      [&peer, &sent_midi](const rtpmidid::io_bytes_reader &data,
                          rtpmidid::rtppeer_t::port_e port) {
        if (peer.is_connected()) {
          data.print_hex();

          auto midi_buffer =
              rtpmidid::io_bytes_reader(data.start + 12, data.size() - 12);
          // 0x47: length 7 and J=1, then the MIDI list, then the empty journal
          // of the first packet (no history yet).
          ASSERT_TRUE(midi_buffer.compare(
              hex_to_bin("47 90 64 7F 68 7F 71 7F 80 00 00")));
          sent_midi = true;
        }
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  peer.send_midi(hex_to_bin("90 64 7F 68 7F 71 7F"));

  ASSERT_TRUE(sent_midi);
}

void test_send_long_midi() {
  rtpmidid::rtppeer_t peer("test");

  bool sent_midi = false;
  auto send_event_c1 = peer.send_event.connect(
      [&peer, &sent_midi](const rtpmidid::io_bytes_reader &data,
                          rtpmidid::rtppeer_t::port_e port) {
        if (peer.is_connected()) {
          data.print_hex();

          auto midi_buffer =
              rtpmidid::io_bytes_reader(data.start + 12, data.size() - 12);
          // Long header: B=1 and J=1 (0xC0) plus the length high nibble, then
          // the MIDI list, then the empty journal of the first packet.
          ASSERT_TRUE(midi_buffer.compare(hex_to_bin(
              "C0 11 F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7 "
              "80 00 00")));
          sent_midi = true;
        }
      });

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  peer.send_midi(
      hex_to_bin("F0 7E 7F 06 02 00 01 0C 00 00 00 03 30 32 32 30 F7"));

  ASSERT_TRUE(sent_midi);
}

void test_recv_some_midi() {
  rtpmidid::rtppeer_t peer("test");

  int got_midi_nr = 0;

  // This will be called when I get some midi data.
  auto midi_event_c1 = peer.midi_event.connect(
      [&peer, &got_midi_nr](const rtpmidid::io_bytes_reader &data) {
        ASSERT_TRUE(peer.is_connected());
        ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::CONNECTED);
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

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  peer.data_ready(
      hex_to_bin(
          "[1000 0001] [0110 0001] 'SQ'"
          "00 00 00 00"
          "'BEEF'"
          "0B"                               // No Journal, 11 bytes
          "90 64 7F 00 90 7F 71 80 80 00 F8" // Two note ons and one clock
          ), // Delta times zero (2 different encodings)
      rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_EQUAL(got_midi_nr, 3);
}

void test_recv_midi_with_running_status() {
  rtpmidid::rtppeer_t peer("test");

  int got_midi_nr = 0;

  // This will be called when I get some midi data.
  auto midi_event_c1 = peer.midi_event.connect(
      [&peer, &got_midi_nr](const rtpmidid::io_bytes_reader &data) {
        ASSERT_TRUE(peer.is_connected());
        ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::CONNECTED);
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

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  peer.data_ready(
      hex_to_bin(
          "[1000 0001] [0110 0001] 'SQ'"
          "00 00 00 00"
          "'BEEF'"
          "09"                         // No Journal, 9 bytes
          "BF 6D 24 00 37 01 00 6D 20" // 3 CC commands on ch15 (running status)
          ),                           // Delta times are zero
      rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_EQUAL(got_midi_nr, 3);
}

/**
 * A lost NoteOff is repaired from the journal of the next packet: this is the
 * stuck note that Chapter N exists for.
 */
void test_journal_repairs_lost_note_off() {
  rtpmidid::rtppeer_t peer("test");

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);

  rtpmidid::io_bytes_writer_static<64> midi_io;
  auto midi_event_c =
      peer.midi_event.connect([&midi_io](const rtpmidid::io_bytes &pb) {
        midi_io.copy_from(pb.start, pb.size());
      });

  // Sequence 0: NoteOn C4.
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"       // Sequence 0
                             "00 00 00 00" // Timestamp
                             "'BEEF'"      // SSRC
                             "03 90 48 40" // NoteOn C4, velocity 64
                             ),
                  rtpmidid::rtppeer_t::MIDI_PORT);

  // Sequence 1 is lost and it carried the NoteOff. Sequence 2 has no MIDI data
  // and a journal whose Chapter N has B=0 (packet I-1 had a NoteOff on this
  // channel) with the NoteOff bit for C4 set.
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 02"       // Sequence 2: sequence 1 is missing
                             "00 00 00 10" // Timestamp
                             "'BEEF'"      // SSRC
                             "40"          // Has journal, no MIDI data
                             // S=0, A=1, TOTCHAN=0, checkpoint sequence 0
                             "20 00 00"
                             // Channel 0: S=0, LENGTH=6, TOC=N
                             "00 06 08"
                             // Chapter N: B=0, LEN=0, LOW=HIGH=9, offbit 72
                             "00 99 80"),
                  rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::CONNECTED);
  DEBUG("MIDI DATA. {} bytes", midi_io.pos());
  midi_io.print_hex();

  // The NoteOn of the stream, then the repaired NoteOff.
  ASSERT_EQUAL(midi_io.pos(), 6);
  ASSERT_EQUAL(midi_io.data[0], 0x90);
  ASSERT_EQUAL(midi_io.data[1], 0x48);
  ASSERT_EQUAL(midi_io.data[2], 0x40);
  ASSERT_EQUAL(midi_io.data[3], 0x80);
  ASSERT_EQUAL(midi_io.data[4], 0x48);
  ASSERT_EQUAL(midi_io.data[5], 0x00);

  ASSERT_EQUAL(peer.recovery_journal.stats.notes_repaired_off, 1);
  ASSERT_EQUAL(peer.recovery_journal.stats.losses, 1);
  ASSERT_EQUAL(peer.recovery_journal.sounding_notes(), 0);
}

/**
 * Peers like Apple's driver set J=1 on every packet. A journal that arrives in
 * order has nothing to repair, and must not emit anything.
 */
void test_journal_in_order_no_spurious_events() {
  rtpmidid::rtppeer_t peer("test");

  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);

  rtpmidid::io_bytes_writer_static<64> midi_io;
  auto midi_event_c =
      peer.midi_event.connect([&midi_io](const rtpmidid::io_bytes &pb) {
        midi_io.copy_from(pb.start, pb.size());
      });

  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"
                             "00 00 00 00"
                             "'BEEF'"
                             "03 90 48 40" // NoteOn C4
                             ),
                  rtpmidid::rtppeer_t::MIDI_PORT);

  // Next packet, in order, carrying the very same journal that repairs the
  // previous test: nothing was lost, so nothing is emitted.
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 01"       // Sequence 1, in order
                             "00 00 00 10" // Timestamp
                             "'BEEF'"
                             "40"
                             "20 00 00"
                             "00 06 08"
                             "00 99 80"),
                  rtpmidid::rtppeer_t::MIDI_PORT);

  DEBUG("MIDI DATA. {} bytes", midi_io.pos());
  midi_io.print_hex();

  ASSERT_EQUAL(midi_io.pos(), 3); // Only the NoteOn
  ASSERT_EQUAL(midi_io.data[0], 0x90);
  ASSERT_EQUAL(peer.recovery_journal.stats.notes_repaired_off, 0);
  ASSERT_EQUAL(peer.recovery_journal.sounding_notes(), 1);
}

/**
 * A peer that says goodbye while a note sounds must not leave it stuck: giving
 * up a session silences every note the receiver believes is sounding (RFC 6295
 * Section 4). Regression: the `BY` path emitted the disconnect event without
 * reset(), so session_end() never ran and the NoteOff was never emitted.
 */
void test_goodbye_silences_sounding_notes() {
  rtpmidid::rtppeer_t peer("test");
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  rtpmidid::io_bytes_writer_static<64> midi_io;
  auto midi_event_c =
      peer.midi_event.connect([&midi_io](const rtpmidid::io_bytes &pb) {
        midi_io.copy_from(pb.start, pb.size());
      });

  // Sequence 0: NoteOn C4, no journal: nothing to repair, just a sounding note.
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"       // Sequence 0
                             "00 00 00 00" // Timestamp
                             "'BEEF'"      // SSRC
                             "03 90 48 40" // NoteOn C4, velocity 64
                             ),
                  rtpmidid::rtppeer_t::MIDI_PORT);
  ASSERT_EQUAL(peer.recovery_journal.sounding_notes(), 1);
  ASSERT_EQUAL(midi_io.pos(), 3);

  // The remote says goodbye on both ports.
  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(DISCONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_EQUAL(peer.status, rtpmidid::rtppeer_t::status_e::NOT_CONNECTED);
  ASSERT_EQUAL(peer.recovery_journal.sounding_notes(), 0);
  ASSERT_EQUAL(peer.recovery_journal.stats.notes_silenced, 1);

  // The NoteOn, then the silencing NoteOff.
  ASSERT_EQUAL(midi_io.pos(), 6);
  ASSERT_EQUAL(midi_io.data[0], 0x90);
  ASSERT_EQUAL(midi_io.data[3], 0x80);
  ASSERT_EQUAL(midi_io.data[4], 0x48);
  ASSERT_EQUAL(midi_io.data[5], 0x00);
}

/**
 * End to end: what one peer sends must repair a loss on the other. The sender
 * journals every packet (J=1) and the receiver repairs what the network ate.
 */
void test_journal_send_and_repair_between_peers() {
  rtpmidid::rtppeer_t sender("sender");
  rtpmidid::rtppeer_t receiver("receiver");
  receiver.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  // The receiver drops packets from an unknown SSRC.
  sender.local_ssrc = receiver.remote_ssrc;

  std::vector<std::vector<uint8_t>> packets;
  auto send_event_c = sender.send_event.connect(
      [&packets](const rtpmidid::io_bytes_reader &data,
                 rtpmidid::rtppeer_t::port_e) {
        packets.emplace_back(data.start, data.end);
      });

  rtpmidid::io_bytes_writer_static<64> midi_io;
  auto midi_event_c =
      receiver.midi_event.connect([&midi_io](const rtpmidid::io_bytes &pb) {
        midi_io.copy_from(pb.start, pb.size());
      });

  sender.status = rtpmidid::rtppeer_t::status_e::CONNECTED;
  sender.send_midi(hex_to_bin("90 48 40")); // Packet 0: NoteOn C4
  sender.send_midi(hex_to_bin("80 48 00")); // Packet 1: NoteOff C4 (gets lost)
  sender.send_midi(hex_to_bin("90 49 40")); // Packet 2: NoteOn D4
  ASSERT_EQUAL(packets.size(), 3);

  // Packets 0 and 2 arrive, packet 1 does not.
  for (size_t i : {size_t(0), size_t(2)}) {
    rtpmidid::io_bytes_reader data(packets[i].data(), packets[i].size());
    receiver.data_ready(std::move(data), rtpmidid::rtppeer_t::MIDI_PORT);
  }

  std::string got_hex;
  for (size_t i = 0; i < midi_io.pos(); i++) {
    got_hex += FMT::format("{:02X} ", midi_io.data[i]);
  }
  INFO("MIDI DATA. {} bytes: {}", midi_io.pos(), got_hex);

  // NoteOn C4, the repaired NoteOff C4, then the NoteOn D4 of packet 2.
  ASSERT_EQUAL(midi_io.pos(), 9);
  ASSERT_EQUAL(midi_io.data[0], 0x90);
  ASSERT_EQUAL(midi_io.data[1], 0x48);
  ASSERT_EQUAL(midi_io.data[3], 0x80);
  ASSERT_EQUAL(midi_io.data[4], 0x48);
  ASSERT_EQUAL(midi_io.data[5], 0x00);
  ASSERT_EQUAL(midi_io.data[6], 0x90);
  ASSERT_EQUAL(midi_io.data[7], 0x49);

  ASSERT_EQUAL(receiver.recovery_journal.stats.losses, 1);
  ASSERT_EQUAL(receiver.recovery_journal.stats.notes_repaired_off, 1);
  ASSERT_EQUAL(receiver.recovery_journal.sounding_notes(), 1); // C4 ended, D4 on
}

/**
 * Apple's journal feedback packet carries a 32 bit sequence number: reading
 * only 16 bits (as the old code did) yields 0 for any real stream.
 */
void test_feedback_reads_32_bit_sequence() {
  rtpmidid::rtppeer_t peer("test");
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);

  peer.data_ready(hex_to_bin("FF FF 'RS'"
                             "'BEEF'"      // SSRC
                             "00 01 00 02" // Extended sequence number
                             ),
                  rtpmidid::rtppeer_t::CONTROL_PORT);

  // Reading 16 bits at offset 8 (the old code) gives the high half, 0x0001.
  ASSERT_EQUAL(peer.seq_nr_ack, 0x0002);
  ASSERT_EQUAL(peer.recovery_journal.has_feedback(), true);
  // Normalized to our rollover count, which is 0: no packet sent yet.
  ASSERT_EQUAL(peer.recovery_journal.confirmed_extended_seq(), 0x0002u);
  ASSERT_EQUAL(peer.recovery_journal.stats.feedback_received, 1);
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

  rtpmidid::rtppeer_t sender("sender");
  rtpmidid::rtppeer_t receiver("receiver");

  auto send_event_c1 = sender.send_event.connect(
      [&receiver](const rtpmidid::io_bytes_reader &data,
                  rtpmidid::rtppeer_t::port_e port) {
        rtpmidid::io_bytes_reader datar(data);
        DEBUG("Write {} bytes to receiver data_ready", data.size());
        receiver.data_ready(std::move(datar), port);
      });

  auto send_event_c2 = receiver.send_event.connect(
      [&sender](const rtpmidid::io_bytes_reader &data,
                rtpmidid::rtppeer_t::port_e port) {
        rtpmidid::io_bytes_reader datar(data);
        DEBUG("Write {} bytes to sender data_ready", data.size());
        sender.data_ready(std::move(datar), port);
      });

  bool got_midi = false;

  auto send_event_c3 = receiver.midi_event.connect(
      [&got_midi](const rtpmidid::io_bytes_reader &midi) {
        INFO("Got MIDI data, size: {}", midi.size());
        // midi.print_hex();
        ASSERT_EQUAL(*midi.position, 0xF0);
        ASSERT_EQUAL(*(midi.end - 1), 0xF7);
        INFO("Got MIDI data, size: {}", midi.size());
        ASSERT_EQUAL(midi.size(), 1026);

        got_midi = true;
      });

  sender.connect_to(rtpmidid::rtppeer_t::CONTROL_PORT);
  sender.connect_to(rtpmidid::rtppeer_t::MIDI_PORT);

  sender.send_midi(sysex);

  ASSERT_TRUE(got_midi);
}

void test_segmented_sysex(void) {
  const auto segmented_sysex1 = hex_to_bin("F0 01 02 03 04 F0");
  const auto segmented_sysex2 = hex_to_bin("F7 05 06 07 08 F7");
  const auto cancel_sysex = hex_to_bin("F7 F4");
  const auto sysex = hex_to_bin("F0 01 02 03 0405 06 07 08 F7");

  rtpmidid::rtppeer_t sender("sender");
  rtpmidid::rtppeer_t receiver("receiver");

  auto send_event_c1 = sender.send_event.connect(
      [&receiver](const rtpmidid::io_bytes_reader &data,
                  rtpmidid::rtppeer_t::port_e port) {
        rtpmidid::io_bytes_reader datar(data);
        DEBUG("Write {} bytes to receiver data_ready", data.size());
        receiver.data_ready(std::move(datar), port);
      });

  auto send_event_c2 = receiver.send_event.connect(
      [&sender](const rtpmidid::io_bytes_reader &data,
                rtpmidid::rtppeer_t::port_e port) {
        rtpmidid::io_bytes_reader datar(data);
        DEBUG("Write {} bytes to sender data_ready", data.size());
        sender.data_ready(std::move(datar), port);
      });
  bool got_data = false;
  auto send_event_c3 = receiver.midi_event.connect(
      [&got_data, &sysex](const rtpmidid::io_bytes_reader &midi) {
        INFO("Got MIDI data");
        // midi.print_hex();
        DEBUG("Got {} bytes, need {} bytes", midi.size(), sysex.size());
        ASSERT_EQUAL(midi.size(), sysex.size());
        ASSERT_EQUAL(memcmp(midi.start, sysex.start, midi.size()), 0);

        got_data = true;
      });

  sender.connect_to(rtpmidid::rtppeer_t::CONTROL_PORT);
  sender.connect_to(rtpmidid::rtppeer_t::MIDI_PORT);

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
      TEST(test_journal_repairs_lost_note_off),
      TEST(test_journal_in_order_no_spurious_events),
      TEST(test_goodbye_silences_sounding_notes),
      TEST(test_feedback_reads_32_bit_sequence),
      TEST(test_journal_send_and_repair_between_peers),
      TEST(test_send_large_sysex),
      TEST(test_segmented_sysex),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
