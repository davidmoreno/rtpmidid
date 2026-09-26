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

/// Write a journal for the packet being sent and return its octets.
static io_bytes_managed write_sender_journal(recovery_journal_t &journal) {
  io_bytes_managed buffer(1024);
  io_bytes_writer writer(buffer);
  journal.write_journal(writer);
  buffer.end = writer.position;
  return buffer;
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
  // The repair knew about a note we did not: that is what the warning reports.
  ASSERT_EQUAL(journal.stats.notes_unknown, 1);
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
  ASSERT_EQUAL(journal.stats.notes_unknown, 1);
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
  // The note was sounding: the lost sequence is a re-trigger, not a note we
  // never knew about.
  ASSERT_EQUAL(journal.stats.notes_unknown, 0u);
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
  // The NoteOff bit for a note we never saw on is not an artifact, but the
  // journal knows about a NoteOn we do not, so it is counted.
  ASSERT_EQUAL(journal.stats.notes_unknown, 1);
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

void test_session_end_silences_sounding_notes() {
  recovery_journal_t journal;
  midi_collector_t out;

  journal.observe(0);
  journal.midi_played(hex_to_bin("90 48 40 91 49 7F"), 100);
  journal.midi_played(hex_to_bin("90 4A 30"), 100);
  ASSERT_EQUAL(journal.sounding_notes(), 3);

  auto silenced = journal.session_end(out.signal);

  ASSERT_EQUAL(silenced, 3);
  ASSERT_EQUAL(out.events.size(), 3);
  // Channel by channel, note by note.
  ASSERT_EQUAL(out.events[0], "80 48 00");
  ASSERT_EQUAL(out.events[1], "80 4A 00");
  ASSERT_EQUAL(out.events[2], "81 49 00");
  ASSERT_EQUAL(journal.sounding_notes(), 0);
  ASSERT_EQUAL(journal.stats.notes_silenced, 3);

  // Nothing sounding: nothing to do.
  out.clear();
  ASSERT_EQUAL(journal.session_end(out.signal), 0);
  ASSERT_EQUAL(out.events.size(), 0);
}

void test_sender_pending_state_and_caught_up() {
  recovery_journal_t journal;
  ASSERT_FALSE(journal.sender_has_pending_state()); // Nothing sent yet
  ASSERT_FALSE(journal.sender_is_caught_up());
  ASSERT_EQUAL(journal.sender_pending_notes(), 0u);

  journal.midi_out(0, hex_to_bin("90 48 40"));
  ASSERT_TRUE(journal.sender_has_pending_state());
  ASSERT_FALSE(journal.sender_is_caught_up()); // No feedback yet
  ASSERT_EQUAL(journal.sender_pending_notes(), 1u);
  ASSERT_EQUAL(journal.last_sent_extended_seq(), 0u);

  // A second packet with a second note: both are unconfirmed, so both are what
  // the guard packet resends.
  journal.midi_out(1, hex_to_bin("90 49 40"));
  ASSERT_EQUAL(journal.sender_pending_notes(), 2u);

  // Feedback that covers the last packet: the peer has everything.
  journal.feedback_in(1);
  ASSERT_TRUE(journal.sender_is_caught_up());
  ASSERT_FALSE(journal.sender_has_pending_state());
  ASSERT_EQUAL(journal.sender_pending_notes(), 0u);

  journal.reset();
  ASSERT_FALSE(journal.sender_has_pending_state());
  ASSERT_EQUAL(journal.sender_pending_notes(), 0u);
}

void test_sender_confirmed() {
  recovery_journal_t journal;
  ASSERT_FALSE(journal.sender_confirmed(0)); // No feedback yet

  journal.midi_out(0, hex_to_bin("90 48 40"));
  journal.midi_out(1, hex_to_bin("00"));
  ASSERT_FALSE(journal.sender_confirmed(0));

  // The peer reports the packet it received: that one and the older ones are
  // confirmed, the newer ones are not. This is the query the guard timer uses
  // to decide that the journal it sent has been delivered.
  journal.feedback_in(0);
  ASSERT_TRUE(journal.sender_confirmed(0));
  ASSERT_FALSE(journal.sender_confirmed(1));
  ASSERT_FALSE(journal.sender_is_caught_up()); // Packet 1 is not confirmed

  journal.feedback_in(1);
  ASSERT_TRUE(journal.sender_confirmed(1));
  ASSERT_TRUE(journal.sender_is_caught_up());
}

void test_feedback_records_extended_seq() {
  recovery_journal_t journal;
  ASSERT_FALSE(journal.has_feedback());

  // The report is normalized to our own rollover count: no packet sent yet, so
  // only the 16 bit sequence number survives.
  journal.feedback_in(0x00010002);
  ASSERT_TRUE(journal.has_feedback());
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0x0002u);
  ASSERT_EQUAL(journal.stats.feedback_received, 1);

  // A peer reports the highest sequence number it has seen, so the value only
  // grows: stale or reordered reports are ignored.
  journal.feedback_in(0x00010001);
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0x0002u);
  journal.feedback_in(0x00010005);
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0x0005u);

  journal.reset();
  ASSERT_FALSE(journal.has_feedback());
  ASSERT_EQUAL(journal.confirmed_extended_seq(), 0u);
}

// ── Sender ──────────────────────────────────────────────────────────────

void test_sender_first_packet_has_empty_journal() {
  recovery_journal_t journal;
  // Packet 0: the journal is written before the packet's own commands are
  // recorded, so there is no history yet.
  auto bytes = write_sender_journal(journal);
  ASSERT_EQUAL(bytes.size(), 3);
  ASSERT_EQUAL(bytes.start[0], 0x80); // S=1, A=0: empty journal
  ASSERT_EQUAL(bytes.start[1], 0x00);
  ASSERT_EQUAL(bytes.start[2], 0x00);

  journal.midi_out(0, hex_to_bin("90 48 40"));
  ASSERT_EQUAL(journal.stats.journals_sent, 1);
}

void test_sender_codes_sounding_notes_from_previous_packet() {
  recovery_journal_t journal;
  write_sender_journal(journal);               // Packet 0
  journal.midi_out(0, hex_to_bin("90 48 40")); // NoteOn C4

  // Packet 1: the note was turned on in packet I-1, so its S bit is 0 (and Y=1:
  // playing it again is what a receiver that lost packet 0 should do).
  auto bytes = write_sender_journal(journal);
  // S=0 A=1 TOTCHAN=0, checkpoint 0
  // channel 0: S=0 (its content comes from packet I-1), LENGTH=7, TOC=N
  // Chapter N: B=1, LEN=1, empty OFFBITS, log S=0 note 0x48 Y=1 vel 0x40
  auto expected = hex_to_bin("20 00 00 | 00 07 08 | 81 F0 48 C0");
  ASSERT_EQUAL(bytes.size(), expected.size());
  ASSERT_TRUE(bytes.compare(expected));

  journal.midi_out(1, hex_to_bin("00")); // No MIDI data in packet 1
}

void test_sender_gear_after_note_off_is_note_off_bit() {
  recovery_journal_t sender;
  recovery_journal_t receiver;
  midi_collector_t out;

  // Packet 0: NoteOn. The receiver gets it.
  write_sender_journal(sender);
  sender.midi_out(0, hex_to_bin("90 48 40"));
  ASSERT_EQUAL(receiver.observe(0), journal_loss_e::none);
  receiver.midi_played(hex_to_bin("90 48 40"), 0);
  ASSERT_EQUAL(receiver.sounding_notes(), 1);

  // Packet 1: NoteOff, which the receiver never gets.
  auto journal1 = write_sender_journal(sender);
  sender.midi_out(1, hex_to_bin("80 48 00"));

  // Packet 2: nothing of its own, and the receiver notices the gap.
  auto journal2 = write_sender_journal(sender);
  sender.midi_out(2, hex_to_bin("00"));

  // The journal of packet 2 must code the NoteOff of packet 1: its B bit is 0
  // (packet I-1 had a NoteOff on this channel) and the OFFBITS bit is set.
  auto expected = hex_to_bin("20 00 00 | 00 06 08 | 00 99 80");
  ASSERT_TRUE(journal2.compare(expected));

  ASSERT_EQUAL(receiver.observe(2), journal_loss_e::single);
  io_bytes_reader reader(journal2);
  receiver.parse_journal(reader, journal_loss_e::single, 200, out.signal);
  receiver.midi_played(hex_to_bin("00"), 200);

  // The stuck note is repaired: this is the whole point of the sender side.
  ASSERT_EQUAL(out.joined(), "80 48 00 | ");
  ASSERT_EQUAL(receiver.sounding_notes(), 0);
  ASSERT_EQUAL(receiver.stats.notes_repaired_off, 1);
  ASSERT_NOT_EQUAL(journal1.size(), 0); // Kept the variable meaningful
}

void test_sender_note_logs_oldest_first() {
  recovery_journal_t journal;
  write_sender_journal(journal);
  journal.midi_out(0, hex_to_bin("90 40 10")); // Note 0x40 first
  write_sender_journal(journal);
  journal.midi_out(1, hex_to_bin("90 30 10")); // Then note 0x30
  write_sender_journal(journal);
  journal.midi_out(2, hex_to_bin("90 50 10")); // Then note 0x50

  // Anchor policy: the checkpoint is the first packet, so all three are coded,
  // in session history order (oldest first), not in note number order. The
  // packet after 2 carries no MIDI data of its own.
  auto bytes = write_sender_journal(journal);
  io_bytes_reader reader(bytes);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.channels.size(), 1);
  const auto &logs = message.channels[0].chapter_n.note_logs;
  ASSERT_EQUAL(logs.size(), 3);
  ASSERT_EQUAL(logs[0].note, 0x40);
  ASSERT_EQUAL(logs[1].note, 0x30);
  ASSERT_EQUAL(logs[2].note, 0x50);
  // Only the note from packet I-1 asks to be played, and only its S bit is 0.
  ASSERT_EQUAL(logs[0].y, false);
  ASSERT_EQUAL(logs[1].y, false);
  ASSERT_EQUAL(logs[2].y, true);
  ASSERT_EQUAL(logs[0].s, true);
  ASSERT_EQUAL(logs[1].s, true);
  ASSERT_EQUAL(logs[2].s, false);
}

/// Regression: the reused per-channel entries must not leak content from an
/// earlier packet into a different channel.
void test_sender_reused_channels_do_not_leak() {
  recovery_journal_t journal;
  write_sender_journal(journal);
  journal.midi_out(0, hex_to_bin("90 40 40 91 45 40")); // ch0 and ch1 sounding

  // Packet 1 codes two channel journals, so the second entry is in use.
  auto two = write_sender_journal(journal);
  io_bytes_reader two_reader(two);
  ASSERT_EQUAL(journal_codec_t::read_journal(two_reader).channels.size(), 2);

  // All Notes Off on channel 1 only: from now on packet 2 codes one channel, so
  // the second entry is left over.
  journal.midi_out(1, hex_to_bin("B1 7B 00"));
  auto one = write_sender_journal(journal);
  io_bytes_reader one_reader(one);
  ASSERT_EQUAL(journal_codec_t::read_journal(one_reader).channels.size(), 1);

  // A note on channel 2 brings the entry back into use. It must not carry the
  // old channel 1 content.
  journal.midi_out(2, hex_to_bin("92 46 40"));
  auto three = write_sender_journal(journal);
  io_bytes_reader three_reader(three);
  auto message = journal_codec_t::read_journal(three_reader);
  ASSERT_EQUAL(message.channels.size(), 2);
  ASSERT_EQUAL(message.channels[0].channel, 0);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs.size(), 1);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs[0].note, 0x40);
  ASSERT_EQUAL(message.channels[1].channel, 2);
  ASSERT_EQUAL(message.channels[1].chapter_n.note_logs.size(), 1);
  ASSERT_EQUAL(message.channels[1].chapter_n.note_logs[0].note, 0x46);
  ASSERT_FALSE(message.channels[1].chapter_n.has_note_off());
}

void test_sender_feedback_shrinks_the_checkpoint() {
  recovery_journal_t journal;
  write_sender_journal(journal);
  journal.midi_out(0, hex_to_bin("90 48 40"));
  auto journal1 = write_sender_journal(journal);
  journal.midi_out(1, hex_to_bin("00"));

  // With no feedback the anchor policy keeps the whole stream in the journal.
  auto expected_anchor = hex_to_bin("20 00 00 | 00 07 08 | 81 F0 48 C0");
  ASSERT_TRUE(journal1.compare(expected_anchor));

  // The peer says it saw packet 1: the checkpoint moves past the NoteOn, so
  // there is nothing left to code.
  journal.feedback_in(1);
  auto journal2 = write_sender_journal(journal);
  auto expected_empty = hex_to_bin("80 00 01");
  ASSERT_TRUE(journal2.compare(expected_empty));
  ASSERT_EQUAL(journal2.size(), 3);

  // Feedback older than what we already sent does not move it back.
  journal.feedback_in(0);
  auto journal3 = write_sender_journal(journal);
  ASSERT_TRUE(journal3.compare(expected_empty));
}

void test_sender_clears_state_on_all_notes_off() {
  recovery_journal_t journal;
  write_sender_journal(journal);
  journal.midi_out(0, hex_to_bin("90 48 40 90 49 40"));
  write_sender_journal(journal);
  journal.midi_out(1, hex_to_bin("B0 7B 00")); // CC 123, All Notes Off

  // The notes are not N-active any more, so the journal has nothing to code.
  // The anchor checkpoint (first packet of the stream) is still coded.
  auto bytes = write_sender_journal(journal);
  ASSERT_TRUE(bytes.compare(hex_to_bin("80 00 00")));
}

void test_sender_mtu_cap_advances_checkpoint() {
  recovery_journal_t journal;
  io_bytes_managed ons(256 * 3);
  {
    io_bytes_writer writer(ons);
    for (uint8_t channel = 0; channel < 2; channel++) {
      for (uint8_t note = 0; note < 128; note++) {
        writer.write_uint8(uint8_t(0x90 | channel));
        writer.write_uint8(note);
        writer.write_uint8(0x40);
      }
    }
    ons.end = writer.position;
  }

  write_sender_journal(journal);
  journal.midi_out(0, ons);
  // One NoteOff, in the packet after the flood.
  journal.midi_out(1, hex_to_bin("80 00 00"));
  write_sender_journal(journal);
  journal.midi_out(2, hex_to_bin("00"));

  // 256 sounding notes need more than max_journal_size octets, so the
  // checkpoint advances past the flood. What is left is the NoteOff bit of
  // packet 1, which is the only command in the new history.
  auto bytes = write_sender_journal(journal);
  ASSERT_LTE(bytes.size(), recovery_journal_t::max_journal_size);
  ASSERT_GT(bytes.size(), 3);
  io_bytes_reader reader(bytes);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_EQUAL(message.channels[0].channel, 0);
  ASSERT_TRUE(message.channels[0].chapter_n.code_note_off(0));
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs.size(), 0);
}

void test_sender_disabled_writes_nothing() {
  recovery_journal_t journal;
  journal.enabled = false;
  journal.midi_out(0, hex_to_bin("90 48 40"));
  auto bytes = write_sender_journal(journal);
  ASSERT_EQUAL(bytes.size(), 0);
  ASSERT_EQUAL(journal.stats.journals_sent, 0);
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
      TEST(test_session_end_silences_sounding_notes),
      TEST(test_sender_pending_state_and_caught_up),
      TEST(test_sender_confirmed),
      TEST(test_feedback_records_extended_seq),
      TEST(test_sender_first_packet_has_empty_journal),
      TEST(test_sender_codes_sounding_notes_from_previous_packet),
      TEST(test_sender_gear_after_note_off_is_note_off_bit),
      TEST(test_sender_note_logs_oldest_first),
      TEST(test_sender_reused_channels_do_not_leak),
      TEST(test_sender_feedback_shrinks_the_checkpoint),
      TEST(test_sender_clears_state_on_all_notes_off),
      TEST(test_sender_mtu_cap_advances_checkpoint),
      TEST(test_sender_disabled_writes_nothing),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
