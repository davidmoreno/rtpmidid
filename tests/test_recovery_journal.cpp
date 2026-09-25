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
#include <rtpmidid/journal.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/signal.hpp>
#include <string>
#include <vector>

using namespace rtpmidid;

/// Collects the MIDI the recovery journal emits, as hex strings.
struct midi_collector_t {
  signal_t<const io_bytes_reader &> signal;
  std::vector<std::string> events;
  signal_t<const io_bytes_reader &>::connection_t connection;

  midi_collector_t() {
    connection = signal.connect([this](const io_bytes_reader &data) {
      std::string hex;
      for (auto *p = data.start; p < data.end; p++) {
        hex += FMT::format("{:02X} ", *p);
      }
      while (!hex.empty() && hex.back() == ' ') {
        hex.pop_back();
      }
      events.push_back(hex);
    });
  }

  void clear() { events.clear(); }
  std::string joined() const {
    std::string out;
    for (const auto &event : events) {
      out += event + " | ";
    }
    return out;
  }
};

/// Build a conforming journal section with the codec under test.
static io_bytes_managed
make_journal(uint16_t checkpoint, bool top_s,
             const std::vector<journal_channel_t> &channels) {
  io_bytes_managed buffer(512);
  io_bytes_writer writer(buffer);
  journal_codec_t::write_journal_n(writer, checkpoint, top_s, channels);
  buffer.end = writer.position;
  return buffer;
}

static journal_channel_t
make_channel(uint8_t channel, journal_chapter_n_t chapter, bool s = true) {
  journal_channel_t result;
  result.channel = channel;
  result.s = s;
  result.has_chapter_n = true;
  result.chapter_n = std::move(chapter);
  return result;
}

// ── Sequence tracking ───────────────────────────────────────────────────

void test_observe_classification() {
  recovery_journal_t journal;
  ASSERT_EQUAL(journal.observe(100), journal_loss_e::none); // first packet
  ASSERT_EQUAL(journal.observe(101), journal_loss_e::none);
  ASSERT_EQUAL(journal.observe(103), journal_loss_e::single);
  ASSERT_EQUAL(journal.observe(108), journal_loss_e::multi);
  ASSERT_EQUAL(journal.stats.losses, 2);

  // A new stream starts from scratch.
  journal.reset();
  ASSERT_EQUAL(journal.observe(5000), journal_loss_e::none);
}

void test_observe_wraparound() {
  recovery_journal_t journal;
  ASSERT_EQUAL(journal.observe(0xFFFE), journal_loss_e::none);
  ASSERT_EQUAL(journal.observe(0x0000), journal_loss_e::single); // wrapped
  ASSERT_EQUAL(journal.observe(0x0001), journal_loss_e::none);
  ASSERT_EQUAL(journal.observe(0x0004), journal_loss_e::multi);
}

void test_observe_out_of_order() {
  recovery_journal_t journal;
  ASSERT_EQUAL(journal.observe(10), journal_loss_e::none);
  ASSERT_EQUAL(journal.observe(10), journal_loss_e::none); // duplicate
  ASSERT_EQUAL(journal.observe(9), journal_loss_e::none);  // reordered
  ASSERT_EQUAL(journal.observe(11), journal_loss_e::none);
  ASSERT_EQUAL(journal.stats.out_of_order, 2);
  ASSERT_EQUAL(journal.stats.losses, 0);
}

// ── Note state (what the receiver believes is sounding) ─────────────────

void test_note_state_tracking() {
  recovery_journal_t journal;

  journal.midi_played(hex_to_bin("90 48 40"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 1);
  ASSERT_TRUE(journal.has_sounding_notes());

  // Several events in one buffer.
  journal.midi_played(hex_to_bin("90 48 40 80 49 00"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 1); // 48 on, 49 off (was not sounded)

  journal.midi_played(hex_to_bin("80 48 40"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 0);

  // NoteOn with zero velocity is a NoteOff.
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  journal.midi_played(hex_to_bin("90 48 00"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 0);

  // Other channels are independent.
  journal.midi_played(hex_to_bin("93 48 40"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 1);
  journal.midi_played(hex_to_bin("80 48 40"), 100); // channel 0, not 3
  ASSERT_EQUAL(journal.sounding_notes(), 1);

  // Garbage without a status byte must not crash or change state.
  journal.midi_played(hex_to_bin("40 40"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 1);

  journal.reset();
  ASSERT_EQUAL(journal.sounding_notes(), 0);
}

void test_note_state_reset_on_all_notes_off() {
  recovery_journal_t journal;
  for (auto controller : {0, 7, 64, 121, 122}) {
    journal.midi_played(hex_to_bin("90 48 40"), 100);
    auto event = FMT::format("B0 {:02X} 00", controller);
    journal.midi_played(hex_to_bin(event), 100);
    ASSERT_EQUAL(journal.sounding_notes(), 1); // not an N-active reset
  }
  for (auto controller : {120, 123, 124, 127}) {
    journal.midi_played(hex_to_bin("90 48 40"), 100);
    auto event = FMT::format("B0 {:02X} 00", controller);
    journal.midi_played(hex_to_bin(event), 100);
    ASSERT_EQUAL(journal.sounding_notes(), 0);
  }

  // System reset.
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  journal.midi_played(hex_to_bin("FF"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 0);
}

void test_note_state_reset_on_reset_state_sysex() {
  // GM System On, with and without the leading F0: the daemon emits reassembled
  // SysEx without it.
  for (const auto &sysex : {"F0 7E 7F 09 01 F7", "7E 7F 09 01 F7"}) {
    recovery_journal_t journal;
    journal.midi_played(hex_to_bin("90 48 40"), 100);
    journal.midi_played(hex_to_bin(sysex), 100);
    ASSERT_EQUAL(journal.sounding_notes(), 0);
  }
  // GM2 on, GM off, DLS on and off.
  for (const auto &sysex : {"F0 7E 00 09 03 F7", "F0 7E 00 09 00 F7",
                            "F0 7E 00 0A 01 F7", "F0 7E 00 0A 02 F7"}) {
    recovery_journal_t journal;
    journal.midi_played(hex_to_bin("90 48 40"), 100);
    journal.midi_played(hex_to_bin(sysex), 100);
    ASSERT_EQUAL(journal.sounding_notes(), 0);
  }
  // A SysEx that is not a Reset State command leaves the notes alone.
  for (const auto &sysex :
       {"F0 7E 00 06 01 F7", "F0 41 00 42 12 40 00 7F 00 41 F7",
        "F0 7E 00 09 02 F7"}) {
    recovery_journal_t journal;
    journal.midi_played(hex_to_bin("90 48 40"), 100);
    journal.midi_played(hex_to_bin(sysex), 100);
    ASSERT_EQUAL(journal.sounding_notes(), 1);
  }
}

// ── Repairs ─────────────────────────────────────────────────────────────

void test_repair_lost_note_off() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 1);

  // Packet 1 was lost, it carried the NoteOff. Its journal is in packet 2: the
  // NoteOff may be missing, so the B bit is 0 (RFC 6295 A.6.1).
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);
  journal_chapter_n_t chapter;
  chapter.b = false;
  chapter.set_note_off(0x48);
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.joined(), "80 48 00 | ");
  ASSERT_EQUAL(journal.sounding_notes(), 0);
  ASSERT_EQUAL(journal.stats.notes_repaired_off, 1);
  ASSERT_EQUAL(journal.stats.journals_received, 1);
}

void test_repair_lost_note_on() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // The lost packet carried a NoteOn: the note log S bit is 0, and Y=1 asks to
  // play it.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, true, false});
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.joined(), "90 48 50 | ");
  ASSERT_EQUAL(journal.sounding_notes(), 1);
  ASSERT_EQUAL(journal.stats.notes_repaired_on, 1);
}

void test_repair_lost_note_on_skipped_by_y() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // Y=0: do not play the recovered note, but still update the state as if it
  // had played, so a later journal does not repair it twice.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, false, false});
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 1);
  ASSERT_EQUAL(journal.stats.notes_skipped, 1);
}

/// The regression guard for the old behaviour: Apple sets J=1 on every packet,
/// and applying those journals when nothing was lost replays notes.
void test_no_repair_when_in_order() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40"), 100);

  ASSERT_EQUAL(journal.observe(1), journal_loss_e::none);

  journal_chapter_n_t chapter;
  chapter.b = false;
  chapter.set_note_off(0x48);
  chapter.note_logs.push_back(journal_note_log_t{0x49, 0x50, true, false});
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::none, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 1);
  ASSERT_EQUAL(journal.stats.notes_repaired_off, 0);
  ASSERT_EQUAL(journal.stats.notes_repaired_on, 0);
  ASSERT_EQUAL(journal.stats.journals_received, 0); // not even parsed
}

void test_single_loss_empty_midi_list_skips() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // S=1 at the top level: the lost packet had no MIDI commands at all, so
  // nothing needs repairing even though the journal looks alarming.
  journal_chapter_n_t chapter;
  chapter.set_note_off(0x48);
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  ASSERT_EQUAL(bytes.start[0] & 0x80, 0x80);
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 1);
}

void test_single_loss_skips_channel_s_bit() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // Top level S=0 (another channel lost something) but this channel journal has
  // S=1: none of its content comes from the lost packet.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, true, true});
  auto bytes = make_journal(0, false, {make_channel(0, chapter, true)});
  ASSERT_EQUAL(bytes.start[0] & 0x80, 0x00);
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 0);
  ASSERT_EQUAL(journal.stats.notes_repaired_on, 0);
}

void test_single_loss_skips_note_log_s_bit() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // The note log has S=1: it codes nothing from the lost packet, so it is not
  // a repair.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, true, true});
  auto bytes = make_journal(0, false, {make_channel(0, chapter, false)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 0);
  ASSERT_EQUAL(journal.stats.notes_repaired_on, 0);
}

void test_multi_loss_parses_everything() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  ASSERT_EQUAL(journal.observe(5), journal_loss_e::multi);

  // With several packets lost the S bits say nothing useful: every element is
  // checked against our state.
  journal_chapter_n_t chapter;
  chapter.set_note_off(0x48); // sounding here: repair it
  chapter.note_logs.push_back(journal_note_log_t{0x49, 0x50, true, true});
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::multi, 200, out.signal);

  // NoteOff bitfield first, then the note logs (RFC 4696 section 7.2).
  ASSERT_EQUAL(out.joined(), "80 48 00 | 90 49 50 | ");
  ASSERT_EQUAL(journal.sounding_notes(), 1); // 48 off, 49 on
}

void test_repair_lost_note_off_on_by_velocity() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 50"), 0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // A NoteOff->NoteOn was lost: the logged velocity differs from ours.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x60, true, false});
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.joined(), "80 48 00 | 90 48 60 | ");
  ASSERT_EQUAL(journal.sounding_notes(), 1);
}

void test_repair_lost_note_off_on_by_checkpoint() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 50"), 0);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // Same velocity, but our NoteOn is older than the journal checkpoint, so
  // there was a NoteOff->NoteOn we never saw. Y=0: repair the NoteOff, skip the
  // NoteOn.
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, false, false});
  auto bytes = make_journal(1, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.joined(), "80 48 00 | ");
  ASSERT_EQUAL(journal.stats.notes_skipped, 1);
}

void test_repair_lost_note_off_on_by_stale_time() {
  {
    recovery_journal_t journal;
    midi_collector_t out;
    journal.observe(0);
    journal.midi_played(hex_to_bin("90 48 50"), 0);
    ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

    // Y=1 says the NoteOn is simultaneous with its packet, but our NoteOn for
    // that note is old: a NoteOff->NoteOn was lost.
    journal_chapter_n_t chapter;
    chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, true, false});
    auto bytes = make_journal(0, true, {make_channel(0, chapter)});
    io_bytes_reader reader(bytes);
    journal.parse_journal(reader, journal_loss_e::single,
                          recovery_journal_t::note_on_recent_window + 1,
                          out.signal);

    ASSERT_EQUAL(out.joined(), "80 48 00 | 90 48 50 | ");
  }
  {
    // Same, but recent: no repair.
    recovery_journal_t journal;
    midi_collector_t out;
    journal.observe(0);
    journal.midi_played(hex_to_bin("90 48 50"), 100);
    ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

    journal_chapter_n_t chapter;
    chapter.note_logs.push_back(journal_note_log_t{0x48, 0x50, true, false});
    auto bytes = make_journal(0, true, {make_channel(0, chapter)});
    io_bytes_reader reader(bytes);
    journal.parse_journal(reader, journal_loss_e::single, 101, out.signal);

    ASSERT_EQUAL(out.events.size(), 0);
    ASSERT_EQUAL(journal.sounding_notes(), 1);
  }
}

void test_no_repair_for_unsounding_note_off_bit() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  ASSERT_EQUAL(journal.observe(5), journal_loss_e::multi);

  // We never played a note on that note number: a set OFFBITS bit is not an
  // artifact to repair.
  journal_chapter_n_t chapter;
  chapter.set_note_off(0x48);
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::multi, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 0);
}

void test_channel_isolation() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("93 48 40"), 100); // channel 3
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  journal_chapter_n_t chapter;
  chapter.b = false;
  chapter.set_note_off(0x48); // ... on channel 0
  auto bytes = make_journal(0, true, {make_channel(0, chapter)});
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.events.size(), 0);
  ASSERT_EQUAL(journal.sounding_notes(), 1); // channel 3 still sounding
}

/**
 * A note log with velocity 0 is not valid per RFC 6295 (it is a NoteOff, coded
 * in the OFFBITS structure), but be liberal and do the right thing.
 */
void test_velocity_zero_note_log_is_note_off() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40"), 100);
  ASSERT_EQUAL(journal.observe(2), journal_loss_e::single);

  // Hand crafted: the codec refuses to write a zero velocity note log.
  // S=0 A=1 TOTCHAN=0, checkpoint 0; channel 0 S=0 LENGTH=7 TOC=N; Chapter N
  // B=1, LEN=1, empty OFFBITS, log S=0 note 0x48 Y=0 velocity 0.
  auto bytes = hex_to_bin("20 00 00 | 00 07 08 | 81 F0 48 00");
  io_bytes_reader reader(bytes);
  journal.parse_journal(reader, journal_loss_e::single, 200, out.signal);

  ASSERT_EQUAL(out.joined(), "80 48 00 | ");
  ASSERT_EQUAL(journal.sounding_notes(), 0);
  ASSERT_EQUAL(journal.stats.notes_repaired_off, 1);
}

void test_feedback_records_extended_seq() {
  recovery_journal_t journal;
  ASSERT_FALSE(journal.has_feedback());
  journal.feedback_in(0x00010002);
  ASSERT_TRUE(journal.has_feedback());
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0x00010002u);
  ASSERT_EQUAL(journal.stats.feedback_received, 1);

  journal.reset();
  ASSERT_FALSE(journal.has_feedback());
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0u);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_observe_classification),
      TEST(test_observe_wraparound),
      TEST(test_observe_out_of_order),
      TEST(test_note_state_tracking),
      TEST(test_note_state_reset_on_all_notes_off),
      TEST(test_note_state_reset_on_reset_state_sysex),
      TEST(test_repair_lost_note_off),
      TEST(test_repair_lost_note_on),
      TEST(test_repair_lost_note_on_skipped_by_y),
      TEST(test_no_repair_when_in_order),
      TEST(test_single_loss_empty_midi_list_skips),
      TEST(test_single_loss_skips_channel_s_bit),
      TEST(test_single_loss_skips_note_log_s_bit),
      TEST(test_multi_loss_parses_everything),
      TEST(test_repair_lost_note_off_on_by_velocity),
      TEST(test_repair_lost_note_off_on_by_checkpoint),
      TEST(test_repair_lost_note_off_on_by_stale_time),
      TEST(test_no_repair_for_unsounding_note_off_bit),
      TEST(test_channel_isolation),
      TEST(test_velocity_zero_note_log_is_note_off),
      TEST(test_feedback_records_extended_seq),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
