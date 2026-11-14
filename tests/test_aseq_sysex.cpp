/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

// ALSA sequencer <-> RTP-MIDI SysEx conversion (issue #113).
//
// ALSA hands over long SysEx commands in several variable length
// SND_SEQ_EVENT_SYSEX events: the kernel bridge parser has a 256 octet buffer
// (sound/core/seq/seq_midi.c, MAX_MIDI_EVENT_BUF) and flushes it whenever it is
// full, so a 420 byte dump arrives as 256 + 164 bytes. Only the event that
// starts the command carries the leading 0xF0 and only the last one carries the
// trailing 0xF7.
//
// RTP-MIDI (RFC 6295 Section 3.3, Figure 5) wants the command as a sequence of
// segments with continuation markers:
//
//   first  : 0xF0 data... 0xF0
//   middle : 0xF7 data... 0xF0
//   last   : 0xF7 data... 0xF7
//
// These tests decode ALSA events (both complete and partial) into that
// segmentation and check that the peer would rebuild exactly the original
// command, octet for octet.

#include "../src/aseq.hpp"
#include "./test_case.hpp"
#include "./test_utils.hpp"
#include <algorithm>
#include <cstring>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <vector>

using rtpmididns::mididata_t;
using rtpmididns::mididata_to_alsaevents_t;

using bytes_t = std::vector<uint8_t>;
using segments_t = std::vector<bytes_t>;

static std::string to_hex(const bytes_t &data, size_t from = 0,
                          size_t to = SIZE_MAX) {
  to = std::min(to, data.size());
  std::string ret;
  for (size_t i = from; i < to; i++) {
    ret += FMT::format("{:02X} ", data[i]);
  }
  return ret;
}

/// Builds a SysEx as a musician would: 0xF0, 7 bit data, 0xF7.
static bytes_t make_sysex(size_t data_size) {
  bytes_t ret;
  ret.reserve(data_size + 2);
  ret.push_back(0xF0);
  for (size_t i = 0; i < data_size; i++) {
    ret.push_back(uint8_t(i * 7) & 0x7F);
  }
  ret.push_back(0xF7);
  return ret;
}

/// An ALSA seq event is a header plus a pointer to the variable length data, so
/// the data buffer must outlive the event.
static snd_seq_event_t make_event(const bytes_t &data, uint8_t client = 0,
                                  uint8_t port = 0) {
  snd_seq_event_t ev{};
  ev.type = SND_SEQ_EVENT_SYSEX;
  ev.flags = SND_SEQ_EVENT_LENGTH_VARIABLE;
  ev.source.client = client;
  ev.source.port = port;
  ev.data.ext.len = data.size();
  ev.data.ext.ptr = const_cast<uint8_t *>(data.data());
  return ev;
}

/// Feeds one ALSA event to the decoder and collects what would be sent over the
/// network.
static void decode_event(mididata_to_alsaevents_t &decoder,
                         const bytes_t &alsa_event, segments_t &out,
                         uint8_t client = 0, uint8_t port = 0) {
  auto ev = make_event(alsa_event, client, port);
  rtpmidid::io_bytes_writer_static<1024> writer;
  decoder.ev_to_mididata_f(&ev, writer, [&out](const mididata_t &midi) {
    out.emplace_back(midi.start, midi.end);
  });
}

/// Splits a SysEx in ALSA events the way the kernel bridge does: the parser
/// buffer is flushed every 256 octets, so only the first event starts with 0xF0
/// and only the last one ends with 0xF7.
static std::vector<bytes_t> alsa_events(const bytes_t &sysex,
                                        size_t chunk = 256) {
  std::vector<bytes_t> ret;
  for (size_t i = 0; i < sysex.size(); i += chunk) {
    auto end = std::min(i + chunk, sysex.size());
    ret.emplace_back(sysex.begin() + i, sysex.begin() + end);
  }
  return ret;
}

/// Checks the segmentation against RFC 6295 Figure 5 and rebuilds the SysEx as
/// an RTP-MIDI receiver does. When the command is still open (the sender is
/// waiting for more data) the last segment legitimately ends with a 0xF0 and no
/// closing 0xF7 is added.
static bytes_t reassemble_sysex(const segments_t &segments, size_t begin = 0,
                                size_t end = SIZE_MAX, bool terminated = true) {
  end = std::min(end, segments.size());
  ASSERT_GT(end, begin);
  bytes_t ret{0xF0};
  for (size_t i = begin; i < end; i++) {
    const auto &segment = segments[i];
    ASSERT_GTE(segment.size(), 2);
    // 256 data octets plus the two markers.
    ASSERT_LTE(segment.size(), 258);
    const bool first = i == begin;
    const bool last = i + 1 == end && terminated;
    ASSERT_EQUAL(int(segment.front()), first ? 0xF0 : 0xF7);
    ASSERT_EQUAL(int(segment.back()), last ? 0xF7 : 0xF0);
    // Only the markers may be status octets.
    for (size_t j = 1; j + 1 < segment.size(); j++) {
      ASSERT_LT(int(segment[j]), 0x80);
    }
    ret.insert(ret.end(), segment.begin() + 1, segment.end() - 1);
  }
  if (terminated) {
    ret.push_back(0xF7);
  }
  return ret;
}

static void assert_reassembles(const segments_t &segments, const bytes_t &sysex,
                               size_t begin = 0, size_t end = SIZE_MAX,
                               bool terminated = true) {
  auto rebuilt = reassemble_sysex(segments, begin, end, terminated);
  ASSERT_EQUAL(to_hex(rebuilt), to_hex(sysex));
}

/// One complete SysEx event is already what RTP-MIDI needs (only split in
/// segments small enough for a network packet).
void test_sysex_single_complete_event() {
  auto sysex = make_sysex(600); // 602 octets, plus the markers
  mididata_to_alsaevents_t decoder;
  segments_t segments;

  decode_event(decoder, sysex, segments);

  // 600 data octets in segments of at most 256.
  ASSERT_EQUAL(segments.size(), 3);
  ASSERT_EQUAL(segments[0].size(), 258);
  ASSERT_EQUAL(segments[1].size(), 258);
  ASSERT_EQUAL(segments[2].size(), 90);
  assert_reassembles(segments, sysex);
}

/// The regression: ALSA delivers the command in 256 octet events. The first one
/// has no 0xF7 and the following ones have no 0xF0, so they must be treated as
/// the start, middle and end of a single command instead of one command each.
void test_sysex_partial_events_256_octets() {
  auto sysex = make_sysex(998); // 1000 octets: 256 + 256 + 256 + 232
  auto events = alsa_events(sysex);
  ASSERT_EQUAL(events.size(), 4);
  ASSERT_EQUAL(events[0].front(), 0xF0);
  ASSERT_NOT_EQUAL(events[0].back(), 0xF7);
  ASSERT_NOT_EQUAL(events[1].front(), 0xF0);
  ASSERT_EQUAL(events[3].back(), 0xF7);

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }

  assert_reassembles(segments, sysex);
}

/// The numbers of issue #113: a 420 octet bulk dump from a hardware port.
void test_sysex_issue_113_420_octets() {
  auto sysex = make_sysex(418); // 420 octets
  auto events = alsa_events(sysex);
  ASSERT_EQUAL(events.size(), 2);
  ASSERT_EQUAL(events[0].size(), 256);
  ASSERT_EQUAL(events[1].size(), 164);

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }

  assert_reassembles(segments, sysex);
}

/// Whatever the size of the ALSA events, the command must survive.
void test_sysex_uneven_partial_events() {
  auto sysex = make_sysex(300);                     // 302 octets
  std::vector<size_t> cuts{0, 1, 7, 260, 261, 301}; // offsets of event starts
  std::vector<bytes_t> events;
  for (size_t i = 0; i < cuts.size(); i++) {
    auto end = i + 1 < cuts.size() ? cuts[i + 1] : sysex.size();
    events.emplace_back(sysex.begin() + cuts[i], sysex.begin() + end);
  }

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }

  assert_reassembles(segments, sysex);
}

/// Chunks of a single octet: 0xF0 alone, data, then 0xF7 alone.
void test_sysex_one_octet_events() {
  auto sysex = make_sysex(4);
  std::vector<bytes_t> events;
  for (auto byte : sysex) {
    events.push_back(bytes_t{byte});
  }

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }

  assert_reassembles(segments, sysex);
}

/// A continuation without a start means we lost the first event (or connected
/// in the middle of a dump). Those data octets are not a MIDI message, so they
/// are dropped instead of being sent as garbage. The next command is fine.
void test_sysex_continuation_without_start_is_dropped() {
  auto sysex = make_sysex(418);
  mididata_to_alsaevents_t decoder;
  segments_t segments;

  decode_event(decoder, bytes_t{0x01, 0x02, 0x03}, segments);
  ASSERT_EQUAL(segments.size(), 0);

  // Still dropping until a 0xF0 shows up, even if this chunk ends the command.
  decode_event(decoder, bytes_t{0x04, 0x05, 0xF7}, segments);
  ASSERT_EQUAL(segments.size(), 0);

  decode_event(decoder, sysex, segments);
  assert_reassembles(segments, sysex);
}

/// A new 0xF0 while a command is open means the sender cancelled the previous
/// one. It is closed with an empty "last" segment (0xF7 0xF7) so it does not
/// swallow the new command.
void test_sysex_restart_closes_unfinished_command() {
  auto unfinished = make_sysex(600);
  unfinished.pop_back(); // no 0xF7: more would come, but never does
  auto sysex = make_sysex(10);

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  decode_event(decoder, unfinished, segments);
  ASSERT_EQUAL(segments.size(), 3);

  decode_event(decoder, sysex, segments);
  ASSERT_EQUAL(segments.size(), 5);
  ASSERT_EQUAL(to_hex(segments[3]), to_hex(bytes_t{0xF7, 0xF7}));

  // The truncated command is delivered as such, and the new one after it.
  ASSERT_EQUAL(to_hex(reassemble_sysex(segments, 0, 3, false)),
               to_hex(unfinished));
  assert_reassembles(segments, sysex, 4, 5);
}

/// A regular MIDI message after a SysEx still goes through the normal path.
void test_midi_after_sysex() {
  auto sysex = make_sysex(418);
  auto events = alsa_events(sysex);

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }
  assert_reassembles(segments, sysex);

  snd_seq_event_t ev{};
  ev.type = SND_SEQ_EVENT_NOTEON;
  ev.data.note.channel = 0;
  ev.data.note.note = 60;
  ev.data.note.velocity = 100;
  rtpmidid::io_bytes_writer_static<16> writer;
  bool got = false;
  decoder.ev_to_mididata_f(&ev, writer, [&got](const mididata_t &midi) {
    got = true;
    ASSERT_EQUAL(midi.end - midi.start, 3);
    ASSERT_EQUAL(int(midi.start[0]), 0x90);
    ASSERT_EQUAL(int(midi.start[1]), 60);
    ASSERT_EQUAL(int(midi.start[2]), 100);
  });
  ASSERT_TRUE(got);
}

/// A SysEx longer than the caller's buffer must not overflow it: the segments
/// are handed over one by one. A client such as aseqsend can send a whole 5 KiB
/// dump as a single event.
void test_sysex_long_event_does_not_overrun_caller_buffer() {
  auto sysex = make_sysex(5000);
  mididata_to_alsaevents_t decoder;
  segments_t segments;

  // 1024 is what the callers in src/local_alsa_*.cpp use.
  auto ev = make_event(sysex);
  rtpmidid::io_bytes_writer_static<1024> writer;
  decoder.ev_to_mididata_f(&ev, writer, [&segments](const mididata_t &midi) {
    segments.emplace_back(midi.start, midi.end);
  });

  // No event is written outside the segments themselves.
  ASSERT_EQUAL(writer.pos(), 0);
  assert_reassembles(segments, sysex);
}

/// An event that does not carry its data inside it (fixed length SysEx) has no
/// valid data pointer: it must be ignored, not followed.
void test_sysex_not_variable_length_is_ignored() {
  mididata_to_alsaevents_t decoder;
  segments_t segments;

  snd_seq_event_t ev{};
  ev.type = SND_SEQ_EVENT_SYSEX;
  ev.flags = SND_SEQ_EVENT_LENGTH_FIXED;
  // A fixed length event carries no data: any pointer here is garbage.
  ev.data.ext.len = 10;
  ev.data.ext.ptr = nullptr;

  rtpmidid::io_bytes_writer_static<1024> writer;
  decoder.ev_to_mididata_f(&ev, writer, [&segments](const mididata_t &midi) {
    segments.emplace_back(midi.start, midi.end);
  });
  ASSERT_EQUAL(segments.size(), 0);
}

/// Two devices can be dumping at the same time: the commands must not get
/// mixed up, so the state is kept per ALSA source port.
void test_sysex_from_two_sources_at_once() {
  auto sysex_a = make_sysex(500);
  auto sysex_b = make_sysex(300);
  auto events_a = alsa_events(sysex_a);
  auto events_b = alsa_events(sysex_b);
  ASSERT_EQUAL(events_a.size(), 2);
  ASSERT_EQUAL(events_b.size(), 2);

  mididata_to_alsaevents_t decoder;
  segments_t segments_a;
  segments_t segments_b;
  for (size_t i = 0; i < events_a.size(); i++) {
    decode_event(decoder, events_a[i], segments_a, 20, 0);
    decode_event(decoder, events_b[i], segments_b, 21, 0);
  }

  assert_reassembles(segments_a, sysex_a);
  assert_reassembles(segments_b, sysex_b);
}

/// A source that sends another message in the middle of a SysEx ended it (there
/// is no 0xF7 in the stream). It is closed so the peer does not keep waiting
/// for the rest of a command that will never arrive.
void test_sysex_interrupted_by_another_message() {
  auto unfinished = make_sysex(400);
  unfinished.pop_back(); // no 0xF7

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  decode_event(decoder, unfinished, segments);
  ASSERT_EQUAL(segments.size(), 2);
  ASSERT_EQUAL(int(segments.back().back()), 0xF0); // still open

  snd_seq_event_t note{};
  note.type = SND_SEQ_EVENT_NOTEON;
  note.data.note.note = 60;
  note.data.note.velocity = 100;

  segments_t out;
  rtpmidid::io_bytes_writer_static<16> writer;
  decoder.ev_to_mididata_f(&note, writer, [&out](const mididata_t &midi) {
    out.emplace_back(midi.start, midi.end);
  });

  // The closing segment, then the note itself.
  ASSERT_EQUAL(out.size(), 2);
  ASSERT_EQUAL(to_hex(out[0]), to_hex(bytes_t{0xF7, 0xF7}));
  ASSERT_EQUAL(to_hex(out[1]), to_hex(bytes_t{0x90, 60, 100}));
}

/// A command of which only the 0xF0 arrived was never sent to the peer, so a
/// new command (or any other message) must not emit a segment for it.
void test_sysex_open_with_nothing_sent() {
  auto sysex = make_sysex(10);
  mididata_to_alsaevents_t decoder;
  segments_t segments;
  decode_event(decoder, bytes_t{0xF0}, segments);
  ASSERT_EQUAL(segments.size(), 0);

  // A new command: no empty closing segment, just the new command.
  decode_event(decoder, sysex, segments);
  ASSERT_EQUAL(segments.size(), 1);
  assert_reassembles(segments, sysex);

  // Same with another message in between.
  mididata_to_alsaevents_t decoder2;
  segments_t segments2;
  decode_event(decoder2, bytes_t{0xF0}, segments2);
  snd_seq_event_t note{};
  note.type = SND_SEQ_EVENT_NOTEON;
  note.data.note.note = 60;
  note.data.note.velocity = 100;
  rtpmidid::io_bytes_writer_static<16> writer;
  decoder2.ev_to_mididata_f(&note, writer, [&segments2](const mididata_t &m) {
    segments2.emplace_back(m.start, m.end);
  });
  ASSERT_EQUAL(segments2.size(), 1);
  ASSERT_EQUAL(to_hex(segments2[0]), to_hex(bytes_t{0x90, 60, 100}));
}

// Same connect message as tests/test_rtppeer.cpp.
static auto CONNECT_MSG = hex_to_bin("FF FF 'IN'"
                                     "0000 0002"
                                     "'FA57' 'BEEF'"
                                     "'testing' 00");

/// End to end, as with issue #113: SysEx partial events from a hardware port
/// must arrive at the network peer as one complete command.
void test_sysex_from_alsa_arrives_complete_at_peer() {
  auto sysex = make_sysex(998);
  auto events = alsa_events(sysex);

  mididata_to_alsaevents_t decoder;
  segments_t segments;
  for (auto &event : events) {
    decode_event(decoder, event, segments);
  }

  rtpmidid::rtppeer_t sender("sender");
  rtpmidid::rtppeer_t receiver("receiver");
  receiver.data_ready(CONNECT_MSG, rtpmidid::rtppeer_t::MIDI_PORT);
  // The receiver drops packets from an unknown SSRC.
  sender.local_ssrc = receiver.remote_ssrc;
  sender.status = rtpmidid::rtppeer_t::status_e::CONNECTED;

  std::vector<bytes_t> packets;
  auto send_event_c = sender.send_event.connect(
      [&packets](const rtpmidid::io_bytes_reader &data,
                 rtpmidid::rtppeer_t::port_e) {
        packets.emplace_back(data.start, data.end);
      });

  bytes_t received;
  auto midi_event_c = receiver.midi_event.connect(
      [&received](const rtpmidid::io_bytes_reader &data) {
        received.insert(received.end(), data.start, data.end);
      });

  // One RTP packet per segment, as the daemon does.
  for (auto &segment : segments) {
    sender.send_midi(rtpmidid::io_bytes_reader(segment.data(), segment.size()));
  }
  ASSERT_EQUAL(packets.size(), segments.size());

  for (auto &packet : packets) {
    rtpmidid::io_bytes_reader data(packet.data(), packet.size());
    receiver.data_ready(std::move(data), rtpmidid::rtppeer_t::MIDI_PORT);
  }

  ASSERT_EQUAL(to_hex(received), to_hex(sysex));
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_sysex_single_complete_event),
      TEST(test_sysex_partial_events_256_octets),
      TEST(test_sysex_issue_113_420_octets),
      TEST(test_sysex_uneven_partial_events),
      TEST(test_sysex_one_octet_events),
      TEST(test_sysex_continuation_without_start_is_dropped),
      TEST(test_sysex_restart_closes_unfinished_command),
      TEST(test_midi_after_sysex),
      TEST(test_sysex_long_event_does_not_overrun_caller_buffer),
      TEST(test_sysex_not_variable_length_is_ignored),
      TEST(test_sysex_from_two_sources_at_once),
      TEST(test_sysex_interrupted_by_another_message),
      TEST(test_sysex_open_with_nothing_sent),
      TEST(test_sysex_from_alsa_arrives_complete_at_peer),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}
