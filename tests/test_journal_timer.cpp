/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/journal_timer.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <vector>

using namespace rtpmidid;
using namespace std::chrono_literals;

auto CONNECT_MSG = hex_to_bin("FF FF 'IN'"
                              "0000 0002"
                              "'FA57' 'BEEF'"
                              "'testing' 00");

/// Packets a peer sent, split by port.
struct sent_packets_t {
  std::vector<std::vector<uint8_t>> control;
  std::vector<std::vector<uint8_t>> midi;
  rtppeer_t::send_event_t::connection_t connection;

  void connect_to(rtppeer_t &peer) {
    connection = peer.send_event.connect(
        [this](const io_bytes_reader &data, rtppeer_t::port_e port) {
          auto &into = port == rtppeer_t::CONTROL_PORT ? control : midi;
          into.emplace_back(data.start, data.end);
        });
  }
};

static bool is_receiver_feedback(const std::vector<uint8_t> &packet) {
  return packet.size() >= 12 && packet[0] == 0xFF && packet[1] == 0xFF &&
         packet[2] == 'R' && packet[3] == 'S';
}

/// A guard packet is an RTP MIDI packet with J=1 and an empty MIDI command
/// list.
static bool is_guard_packet(const std::vector<uint8_t> &packet) {
  if (packet.size() < 16 || packet[0] != 0x80 || packet[1] != 0x61) {
    return false;
  }
  const uint8_t command_section = packet[12];
  if ((command_section & 0x40) == 0) {
    return false; // No journal
  }
  if ((command_section & 0x80) != 0) {
    return (command_section & 0x0F) == 0 && (packet[13] & 0x0F) == 0;
  }
  return (command_section & 0x0F) == 0;
}

static size_t
guard_packet_count(const std::vector<std::vector<uint8_t>> &packets) {
  size_t count = 0;
  for (const auto &packet : packets) {
    if (is_guard_packet(packet)) {
      count++;
    }
  }
  return count;
}

void test_timer_sends_feedback_and_guard_packets() {
  rtpmidid::rtppeer_t peer("test");
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  ASSERT_TRUE(peer.is_connected());

  sent_packets_t sent;
  sent.connect_to(peer);

  journal_timer_t timer(peer);
  timer.tick_period = 2ms;
  timer.feedback_period = 10ms;
  timer.guard_min_period = 10ms;
  timer.guard_max_period = 20ms;

  // Something to report back to the peer...
  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"       // Sequence 0
                             "00 00 00 00" // Timestamp
                             "'BEEF'"      // SSRC
                             "03 90 48 40" // NoteOn C4
                             ),
                  rtpmidid::rtppeer_t::MIDI_PORT);
  // ... and something to guard.
  peer.send_midi(hex_to_bin("90 49 40"));
  ASSERT_EQUAL(peer.recovery_journal.sender_has_pending_state(), true);

  // Wait for the timer to do its job: the first tick sends the feedback and
  // then the first guard packet.
  poller_wait_until(
      [&sent]() {
        return !sent.control.empty() && guard_packet_count(sent.midi) > 0;
      },
      500ms);
  poller_wait_for(50ms);

  // Receiver feedback: 'RS', our SSRC, and the extended sequence of the highest
  // packet received.
  ASSERT_GT(sent.control.size(), 0u);
  ASSERT_TRUE(is_receiver_feedback(sent.control[0]));
  ASSERT_EQUAL(sent.control[0][8], 0x00);
  ASSERT_EQUAL(sent.control[0][9], 0x00);
  ASSERT_EQUAL(sent.control[0][10], 0x00);
  ASSERT_EQUAL(sent.control[0][11], 0x00); // Sequence 0 was the only one
  ASSERT_EQUAL(peer.recovery_journal.stats.feedback_sent > 0, true);

  // The packet with real MIDI went out first, then guard packets: journal only,
  // repeated with backoff while the peer confirms nothing.
  ASSERT_GT(sent.midi.size(), 1u);
  ASSERT_FALSE(is_guard_packet(sent.midi[0]));
  ASSERT_GT(guard_packet_count(sent.midi), 0u);
  ASSERT_EQUAL(guard_packet_count(sent.midi),
               peer.recovery_journal.stats.guard_packets);
}

void test_timer_stops_guards_when_the_peer_is_caught_up() {
  rtpmidid::rtppeer_t peer("test");
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  sent_packets_t sent;
  sent.connect_to(peer);

  journal_timer_t timer(peer);
  timer.tick_period = 2ms;
  timer.feedback_period = 5ms;
  timer.guard_min_period = 5ms;
  timer.guard_max_period = 10ms;

  peer.data_ready(hex_to_bin("[1000 0001] [0110 0001] "
                             "00 00"
                             "00 00 00 00"
                             "'BEEF'"
                             "03 90 48 40"),
                  rtpmidid::rtppeer_t::MIDI_PORT);
  peer.send_midi(hex_to_bin("90 49 40"));

  // Wait for the timer to report and to start guarding.
  poller_wait_until(
      [&sent]() {
        return !sent.control.empty() && guard_packet_count(sent.midi) > 0;
      },
      500ms);
  ASSERT_GT(guard_packet_count(sent.midi), 0u);
  ASSERT_GT(sent.control.size(), 0u);

  // The peer reports it saw our last packet: nothing left to guard, so the
  // guard packets must stop while the feedback keeps flowing.
  peer.recovery_journal.feedback_in(peer.seq_nr);
  ASSERT_EQUAL(peer.recovery_journal.sender_is_caught_up(), true);

  auto guards_when_caught_up = sent.midi.size();
  auto feedback_when_caught_up = sent.control.size();
  poller_wait_for(50ms);

  ASSERT_EQUAL(sent.midi.size(), guards_when_caught_up);
  ASSERT_GT(sent.control.size(), feedback_when_caught_up);
}

void test_timer_stops_when_disconnected() {
  rtpmidid::rtppeer_t peer("test");
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);

  sent_packets_t sent;
  sent.connect_to(peer);

  journal_timer_t timer(peer);
  timer.tick_period = 2ms;
  timer.feedback_period = 5ms;

  ASSERT_TRUE(timer.is_running());

  // Goodbye from the peer.
  peer.data_ready(hex_to_bin("FF FF 'BY'"
                             "0000 0002"
                             "'FA57' 'BEEF'"),
                  rtpmidid::rtppeer_t::CONTROL_PORT);
  peer.data_ready(hex_to_bin("FF FF 'BY'"
                             "0000 0002"
                             "'FA57' 'BEEF'"),
                  rtpmidid::rtppeer_t::MIDI_PORT);

  ASSERT_FALSE(peer.is_connected());
  ASSERT_FALSE(timer.is_running());

  auto packets = sent.control.size() + sent.midi.size();
  poller_wait_for(30ms);
  ASSERT_EQUAL(sent.control.size() + sent.midi.size(), packets);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_timer_sends_feedback_and_guard_packets),
      TEST(test_timer_stops_guards_when_the_peer_is_caught_up),
      TEST(test_timer_stops_when_disconnected),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
