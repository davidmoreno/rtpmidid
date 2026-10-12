/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

// The hot path must not allocate once a session is up. Each test here sets up
// (and warms up) whatever state the path needs, then asserts that the steady
// state performs zero heap allocations. See malloc_counter.hpp for how the
// count is taken; a regression that reintroduces an allocation in any of these
// paths fails the test with the number of allocations it made.

#include "lite/sysex.hpp"
#include "malloc_counter.hpp"
#include "test_case.hpp"

#include <cstring>
#include <functional>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <vector>

namespace {

// Same 'IN' packet the other tests connect with: initiator 'FA57', ssrc 'BEEF'.
const uint8_t CONNECT_MSG[] = {0xFF, 0xFF, 'I', 'N', 0x00, 0x00, 0x00, 0x02,
                               'F',  'A',  '5', '7', 'B',  'E',  'E',  'F',
                               't',  'e',  's', 't', 'i',  'n',  'g',  0x00};

/// Connect a peer on both ports, ignoring whatever it answers.
void connect_peer(rtpmidid::rtppeer_t &peer) {
  auto in = rtpmidid::io_bytes_reader(const_cast<uint8_t *>(CONNECT_MSG),
                                      sizeof(CONNECT_MSG));
  peer.data_ready(rtpmidid::io_bytes_reader(in),
                  rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(rtpmidid::io_bytes_reader(in),
                  rtpmidid::rtppeer_t::MIDI_PORT);
}

/// Build an RTP-MIDI packet (12 octet header + payload) into a static buffer,
/// so the test itself does not allocate while feeding the peer.
uint8_t g_packet[2048];
size_t build_packet(uint16_t seq, const uint8_t *payload, size_t len) {
  const uint8_t header[] = {0x81,
                            0x61,
                            uint8_t(seq >> 8),
                            uint8_t(seq & 0xFF),
                            0x00,
                            0x00,
                            0x00,
                            0x00,
                            'B',
                            'E',
                            'E',
                            'F'};
  std::memcpy(g_packet, header, sizeof(header));
  std::memcpy(g_packet + sizeof(header), payload, len);
  return sizeof(header) + len;
}

void feed(rtpmidid::rtppeer_t &peer, uint16_t seq, const uint8_t *payload,
          size_t len) {
  const auto size = build_packet(seq, payload, len);
  peer.data_ready(rtpmidid::io_bytes_reader(g_packet, size),
                  rtpmidid::rtppeer_t::MIDI_PORT);
}

} // namespace

void test_send_midi_does_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  uint8_t note_on[] = {0x90, 0x40, 0x7F};
  auto on = rtpmidid::io_bytes_reader(note_on, sizeof(note_on));
  peer.send_midi(on); // Warm up the coded channel state

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    peer.send_midi(on);
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(allocations, size_t(0));
}

void test_send_full_journal_does_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  // Sixteen channels with eight sounding notes each: a full Chapter N journal.
  std::vector<uint8_t> chord;
  for (uint8_t channel = 0; channel < 16; channel++)
    for (uint8_t note = 0; note < 8; note++)
      chord.insert(chord.end(),
                   {uint8_t(0x90 | channel), uint8_t(60 + note), 0x7F});
  auto events = rtpmidid::io_bytes_reader(chord.data(), chord.size());
  peer.send_midi(events); // Warm up

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    peer.send_midi(events);
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(allocations, size_t(0));
}

void test_guard_and_feedback_do_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  // A sounding note makes the sender have pending state, so guards are sent.
  uint8_t note_on[] = {0x90, 0x40, 0x7F};
  peer.send_midi(rtpmidid::io_bytes_reader(note_on, sizeof(note_on)));
  // A received packet is needed before feedback is sent.
  uint8_t midi[] = {0x03, 0x90, 0x48, 0x40};
  feed(peer, 0, midi, sizeof(midi));

  peer.send_journal_packet(); // Warm up

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    peer.send_journal_packet();
  const auto guard_allocations = malloc_counter::count();
  malloc_counter::stop();
  ASSERT_EQUAL(guard_allocations, size_t(0));

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    peer.send_feedback();
  const auto feedback_allocations = malloc_counter::count();
  malloc_counter::stop();
  ASSERT_EQUAL(feedback_allocations, size_t(0));
}

void test_recv_in_order_does_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  uint8_t midi[] = {0x03, 0x90, 0x48, 0x40};
  uint16_t seq = 0;
  for (int i = 0; i < 10; i++)
    feed(peer, seq++, midi, sizeof(midi)); // Warm up

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    feed(peer, seq++, midi, sizeof(midi));
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(allocations, size_t(0));
}

void test_recv_running_status_does_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  // Three CC commands on channel 15, the last two with running status (the
  // status octet is only present on the first). This used to allocate an
  // io_bytes_managed per abbreviated command.
  uint8_t running_status[] = {0x09, 0xBF, 0x6D, 0x24, 0x00,
                              0x37, 0x01, 0x00, 0x6D, 0x20};
  uint16_t seq = 0;
  for (int i = 0; i < 10; i++)
    feed(peer, seq++, running_status, sizeof(running_status)); // Warm up

  malloc_counter::start();
  for (int i = 0; i < 1000; i++)
    feed(peer, seq++, running_status, sizeof(running_status));
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(allocations, size_t(0));
}

void test_recv_journal_repair_does_not_allocate() {
  rtpmidid::rtppeer_t peer("malloc");
  connect_peer(peer);

  uint8_t note_on[] = {0x03, 0x90, 0x48, 0x40};
  // Journal carrying only a Chapter N OFFBITS (the repairable lost NoteOff).
  uint8_t journal_offbits[] = {0x40, 0x20, 0x00, 0x00, 0x00,
                               0x06, 0x08, 0x00, 0x99, 0x80};
  // Journal carrying a Chapter N note log (S=0, so it counts for a single
  // loss).
  uint8_t journal_note_log[] = {0x40, 0x20, 0x00, 0x00, 0x00, 0x07,
                                0x08, 0x81, 0xF0, 0x48, 0xFF};

  uint16_t seq = 0;
  auto loss_cycle = [&](const uint8_t *journal, size_t len) {
    feed(peer, seq, note_on, sizeof(note_on));
    // Sequence seq + 1 is lost; seq + 2 carries the journal.
    feed(peer, uint16_t(seq + 2), journal, len);
    seq = uint16_t(seq + 3);
  };

  // Warm up: the first repair sizes the reusable decode buffer.
  loss_cycle(journal_offbits, sizeof(journal_offbits));
  loss_cycle(journal_note_log, sizeof(journal_note_log));

  malloc_counter::start();
  for (int i = 0; i < 200; i++) {
    loss_cycle(journal_offbits, sizeof(journal_offbits));
    loss_cycle(journal_note_log, sizeof(journal_note_log));
  }
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(allocations, size_t(0));
}

void test_lite_split_sysex_does_not_allocate() {
  std::vector<uint8_t> message(2 + 1024, 0x11);
  message.front() = 0xF0;
  message.back() = 0xF7;

  size_t segments = 0;
  std::function<void(const uint8_t *, size_t)> emit =
      [&segments](const uint8_t *, size_t) { segments++; };

  auto split = [&]() {
    rtpmidid_lite::split_sysex<rtpmidid_lite::SYSEX_SEGMENT_BYTES>(
        message.data(), message.size(), rtpmidid_lite::SYSEX_SEGMENT_BYTES,
        emit);
  };
  split(); // Warm up
  segments = 0;

  malloc_counter::start();
  for (int i = 0; i < 10; i++)
    split();
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  // 1024 payload octets at 256 per segment: four segments per message.
  ASSERT_EQUAL(segments, size_t(40));
  ASSERT_EQUAL(allocations, size_t(0));
}

void test_lite_sysex_assembler_reuses_capacity() {
  rtpmidid_lite::sysex_assembler_t assembler;
  std::vector<uint8_t> message(4096, 0x22);
  message.front() = 0xF0;
  message.back() = 0xF7;

  size_t complete = 0;
  auto emit = [&complete](const uint8_t *, size_t) { complete++; };
  auto feed_all = [&]() {
    for (size_t offset = 0; offset < message.size(); offset += 64)
      assembler.feed(message.data() + offset, 64, emit);
  };

  feed_all(); // First message grows the reassembly buffer

  malloc_counter::start();
  feed_all(); // Same message again: the buffer keeps its capacity
  const auto allocations = malloc_counter::count();
  malloc_counter::stop();

  ASSERT_EQUAL(complete, size_t(2));
  ASSERT_EQUAL(allocations, size_t(0));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_send_midi_does_not_allocate),
      TEST(test_send_full_journal_does_not_allocate),
      TEST(test_guard_and_feedback_do_not_allocate),
      TEST(test_recv_in_order_does_not_allocate),
      TEST(test_recv_running_status_does_not_allocate),
      TEST(test_recv_journal_repair_does_not_allocate),
      TEST(test_lite_split_sysex_does_not_allocate),
      TEST(test_lite_sysex_assembler_reuses_capacity),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
