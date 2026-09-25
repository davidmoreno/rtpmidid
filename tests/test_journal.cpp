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
#include <vector>

using namespace rtpmidid;

static std::string hex_of(io_bytes &bytes, size_t count) {
  std::string out;
  for (size_t i = 0; i < count; i++) {
    out += FMT::format("{:02X} ", bytes.start[i]);
  }
  return out;
}

/**
 * Assert the bytes written so far ([start, position)) are exactly the given
 * hex string, as parsed by hex_to_bin (spaces, brackets and quotes ignored).
 */
static void assert_written(io_bytes_writer &writer, const std::string &hex) {
  auto expected = hex_to_bin(hex);
  io_bytes_reader got(writer);
  io_bytes_reader want(expected);
  if (got.compare(want)) {
    return;
  }
  ERROR("Expected {} octets: {}", want.size(), hex_of(want, want.size()));
  ERROR("Got {} octets: {}", got.size(), hex_of(got, got.size()));
  ASSERT_EQUAL(got.size(), want.size());
  ASSERT_TRUE(false);
}

// ── Top-level header ────────────────────────────────────────────────────

void test_header_encode() {
  // Empty journal: S=1, no system journal, no channel journals.
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_header(
        writer, journal_header_t{true, false, false, false, 0, 0x0000});
    assert_written(writer, "80 00 00");
  }
  // Channel journals, S=0, 3 channels (TOTCHAN=2).
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_header(
        writer, journal_header_t{false, true, true, false, 2, 0x1234});
    assert_written(writer, "62 12 34");
  }
  // All bits set, maximum TOTCHAN.
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_header(
        writer, journal_header_t{true, true, true, true, 15, 0xFFFF});
    assert_written(writer, "FF FF FF");
  }
}

void test_header_roundtrip() {
  for (bool s : {false, true}) {
    for (bool y : {false, true}) {
      for (bool h : {false, true}) {
        for (uint8_t totchan : {uint8_t(0), uint8_t(3), uint8_t(15)}) {
          journal_header_t header{s, y, true, h, totchan, 0xBEEF};
          io_bytes_writer_static<8> writer;
          journal_codec_t::write_header(writer, header);
          ASSERT_EQUAL(writer.pos(), journal_codec_t::header_size);

          io_bytes_reader reader(writer);
          auto decoded = journal_codec_t::read_header(reader);
          ASSERT_EQUAL(decoded.s, header.s);
          ASSERT_EQUAL(decoded.y, header.y);
          ASSERT_EQUAL(decoded.a, header.a);
          ASSERT_EQUAL(decoded.h, header.h);
          ASSERT_EQUAL(decoded.totchan, header.totchan);
          ASSERT_EQUAL(decoded.checkpoint, header.checkpoint);
          ASSERT_EQUAL(decoded.channel_count(), header.totchan + 1);
        }
      }
    }
  }
}

void test_header_channel_count() {
  ASSERT_EQUAL(journal_header_t{}.channel_count(), 0);
  journal_header_t header;
  header.a = true;
  header.totchan = 2;
  ASSERT_EQUAL(header.channel_count(), 3);
}

void test_header_rejects_invalid_totchan() {
  io_bytes_writer_static<8> writer;
  bool thrown = false;
  try {
    // A=1 with a 4 bit TOTCHAN of 16 is not expressible.
    journal_codec_t::write_header(
        writer, journal_header_t{true, false, true, false, 16, 0});
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);

  thrown = false;
  try {
    journal_codec_t::write_header(
        writer, journal_header_t{true, false, false, false, 1, 0});
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

// ── Channel journal header ──────────────────────────────────────────────

void test_channel_header_encode() {
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_channel_header(
        writer,
        journal_channel_header_t{false, 9, true, 0x123, JOURNAL_CHAPTER_N});
    assert_written(writer, "4D 23 08");
  }
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_channel_header(
        writer,
        journal_channel_header_t{true, 15, false, 8, JOURNAL_CHAPTER_N});
    assert_written(writer, "F8 08 08");
  }
  // Maximum 10 bit length, minimum channel.
  {
    io_bytes_writer_static<8> writer;
    journal_codec_t::write_channel_header(
        writer, journal_channel_header_t{
                    true, 0, false, 0x3FF,
                    uint8_t(JOURNAL_CHAPTER_P | JOURNAL_CHAPTER_C)});
    assert_written(writer, "83 FF C0");
  }
}

void test_channel_header_roundtrip() {
  for (bool s : {false, true}) {
    for (uint8_t channel = 0; channel < 16; channel++) {
      for (uint16_t length : {uint16_t(3), uint16_t(0x123), uint16_t(0x3FF)}) {
        journal_channel_header_t header{s, channel, true, length, 0xF8};
        io_bytes_writer_static<8> writer;
        journal_codec_t::write_channel_header(writer, header);
        ASSERT_EQUAL(writer.pos(), journal_codec_t::channel_header_size);

        io_bytes_reader reader(writer);
        auto decoded = journal_codec_t::read_channel_header(reader);
        ASSERT_EQUAL(decoded.s, header.s);
        ASSERT_EQUAL(decoded.channel, header.channel);
        ASSERT_EQUAL(decoded.h, header.h);
        ASSERT_EQUAL(decoded.length, header.length);
        ASSERT_EQUAL(decoded.toc, header.toc);
      }
    }
  }
}

void test_channel_header_rejects_short_length() {
  io_bytes_writer_static<8> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_channel_header(
        writer, journal_channel_header_t{true, 0, false, 2, JOURNAL_CHAPTER_N});
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_channel_header_reads_rfc_figure() {
  // |S| CHAN(4) |H| LENGTH(10) |: S=1, CHAN=3, H=0, LENGTH=17, TOC=P|C|M|W|N
  auto bin = hex_to_bin("98 11 F8");
  io_bytes_reader reader(bin);
  auto header = journal_codec_t::read_channel_header(reader);
  ASSERT_EQUAL(header.s, true);
  ASSERT_EQUAL(header.channel, 3);
  ASSERT_EQUAL(header.h, false);
  ASSERT_EQUAL(header.length, 17);
  ASSERT_EQUAL(header.toc, 0xF8);
}

// ── Chapter N ───────────────────────────────────────────────────────────

void test_chapter_n_empty() {
  journal_chapter_n_t chapter;
  io_bytes_writer_static<8> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  // B=1, LEN=0, LOW=15, HIGH=0: empty note list and empty NoteOff bitfield.
  assert_written(writer, "80 F0");
  ASSERT_EQUAL(journal_codec_t::chapter_n_size(chapter), 2);

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.note_logs.size(), 0);
  ASSERT_FALSE(decoded.has_note_off());
  ASSERT_TRUE(decoded.empty());
}

void test_chapter_n_note_log() {
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x7F, true, true});
  io_bytes_writer_static<8> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  assert_written(writer, "81 F0 C8 FF");

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.note_logs.size(), 1);
  ASSERT_EQUAL(decoded.note_logs[0].note, 0x48);
  ASSERT_EQUAL(decoded.note_logs[0].velocity, 0x7F);
  ASSERT_EQUAL(decoded.note_logs[0].y, true);
  ASSERT_EQUAL(decoded.note_logs[0].s, true);
  ASSERT_EQUAL(decoded.b, true);
  ASSERT_FALSE(decoded.has_note_off());
}

void test_chapter_n_s_and_y_bits() {
  journal_chapter_n_t chapter;
  chapter.b = false;
  chapter.note_logs.push_back(journal_note_log_t{0x48, 0x40, false, false});
  io_bytes_writer_static<8> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  assert_written(writer, "01 F0 48 40");

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.b, false);
  ASSERT_EQUAL(decoded.note_logs[0].y, false);
  ASSERT_EQUAL(decoded.note_logs[0].s, false);
}

void test_chapter_n_offbits_one_octet() {
  journal_chapter_n_t chapter;
  chapter.set_note_off(72); // C4
  chapter.set_note_off(75);
  io_bytes_writer_static<8> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  // LEN=0; LOW=HIGH=9 (notes 72..79); bits 0 (0x80) and 3 (0x10) set.
  assert_written(writer, "80 99 90");

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.note_logs.size(), 0);
  ASSERT_TRUE(decoded.has_note_off());
  ASSERT_TRUE(decoded.code_note_off(72));
  ASSERT_FALSE(decoded.code_note_off(73));
  ASSERT_TRUE(decoded.code_note_off(75));
  ASSERT_FALSE(decoded.code_note_off(76));
}

void test_chapter_n_offbits_multi_octet() {
  journal_chapter_n_t chapter;
  chapter.set_note_off(71); // last note of octet 8
  chapter.set_note_off(80); // first note of octet 10
  io_bytes_writer_static<8> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  // LOW=8, HIGH=10; octet 8: bit 7 (0x01); octet 9: none; octet 10: bit 0
  // (0x80)
  assert_written(writer, "80 8A 01 00 80");

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_FALSE(decoded.code_note_off(70));
  ASSERT_TRUE(decoded.code_note_off(71));
  ASSERT_FALSE(decoded.code_note_off(79));
  ASSERT_TRUE(decoded.code_note_off(80));
  ASSERT_FALSE(decoded.code_note_off(81));
}

void test_chapter_n_128_note_logs() {
  journal_chapter_n_t chapter;
  for (uint8_t note = 0; note < 128; note++) {
    chapter.note_logs.push_back(journal_note_log_t{note, 1, true, true});
  }
  io_bytes_writer_static<300> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  // LEN field is 127 (0xFF) with LOW=15, HIGH=0 (0xF0).
  ASSERT_EQUAL(writer.start[0], 0xFF);
  ASSERT_EQUAL(writer.start[1], 0xF0);
  ASSERT_EQUAL(writer.pos(), 2 + 128 * 2);
  ASSERT_EQUAL(journal_codec_t::chapter_n_size(chapter), writer.pos());

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.note_logs.size(), 128);
  ASSERT_FALSE(decoded.has_note_off());
  ASSERT_EQUAL(decoded.note_logs[127].note, 127);
}

void test_chapter_n_127_note_logs_with_offbits() {
  journal_chapter_n_t chapter;
  for (uint8_t note = 0; note < 127; note++) {
    chapter.note_logs.push_back(journal_note_log_t{note, 0x40, true, true});
  }
  chapter.set_note_off(127);
  io_bytes_writer_static<300> writer;
  journal_codec_t::write_chapter_n(writer, chapter);
  // LEN=127 but with a NoteOff bit, so it codes 127 logs, not 128.
  ASSERT_EQUAL(writer.start[0], 0xFF);
  ASSERT_EQUAL(writer.start[1], 0xFF);
  ASSERT_EQUAL(writer.pos(), 2 + 127 * 2 + 1);

  io_bytes_reader reader(writer);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_EQUAL(decoded.note_logs.size(), 127);
  ASSERT_TRUE(decoded.code_note_off(127));
}

void test_chapter_n_roundtrip() {
  // Deterministic sweep: every note number as a log, as a NoteOff bit, and
  // both with mixed flags, across a couple of b/s combinations.
  for (uint8_t seed = 0; seed < 4; seed++) {
    journal_chapter_n_t chapter;
    chapter.b = (seed & 1) != 0;
    for (uint8_t note = 0; note < 128; note++) {
      switch ((note + seed) % 3) {
      case 0:
        chapter.note_logs.push_back(journal_note_log_t{
            note, uint8_t(1 + (note % 127)), (note % 2) == 0, (note % 5) != 0});
        break;
      case 1:
        chapter.set_note_off(note);
        break;
      default:
        break;
      }
    }
    io_bytes_writer_static<300> writer;
    journal_codec_t::write_chapter_n(writer, chapter);
    ASSERT_EQUAL(journal_codec_t::chapter_n_size(chapter), writer.pos());

    io_bytes_reader reader(writer);
    auto decoded = journal_codec_t::read_chapter_n(reader);
    ASSERT_EQUAL(reader.pos(), writer.pos());
    ASSERT_EQUAL(decoded.b, chapter.b);
    ASSERT_EQUAL(decoded.note_logs.size(), chapter.note_logs.size());
    for (size_t i = 0; i < decoded.note_logs.size(); i++) {
      ASSERT_EQUAL(decoded.note_logs[i].note, chapter.note_logs[i].note);
      ASSERT_EQUAL(decoded.note_logs[i].velocity,
                   chapter.note_logs[i].velocity);
      ASSERT_EQUAL(decoded.note_logs[i].y, chapter.note_logs[i].y);
      ASSERT_EQUAL(decoded.note_logs[i].s, chapter.note_logs[i].s);
    }
    for (uint8_t note = 0; note < 128; note++) {
      ASSERT_EQUAL(decoded.note_off[note], chapter.note_off[note]);
    }
  }
}

void test_chapter_n_rejects_zero_velocity_log() {
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x40, 0, true, true});
  io_bytes_writer_static<8> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_chapter_n(writer, chapter);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_rejects_note_in_both_structures() {
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x40, 0x40, true, true});
  chapter.set_note_off(0x40);
  io_bytes_writer_static<8> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_chapter_n(writer, chapter);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_rejects_duplicate_note_log() {
  journal_chapter_n_t chapter;
  chapter.note_logs.push_back(journal_note_log_t{0x40, 0x40, true, true});
  chapter.note_logs.push_back(journal_note_log_t{0x40, 0x50, true, true});
  io_bytes_writer_static<8> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_chapter_n(writer, chapter);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_rejects_too_many_logs() {
  journal_chapter_n_t chapter;
  for (uint16_t i = 0; i < 129; i++) {
    chapter.note_logs.push_back(
        journal_note_log_t{uint8_t(i % 128), 1, true, true});
  }
  io_bytes_writer_static<400> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_chapter_n(writer, chapter);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_rejects_128_logs_with_offbits() {
  journal_chapter_n_t chapter;
  for (uint8_t note = 0; note < 128; note++) {
    chapter.note_logs.push_back(journal_note_log_t{note, 1, true, true});
  }
  chapter.set_note_off(0);
  io_bytes_writer_static<300> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_chapter_n(writer, chapter);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_reads_empty_offbits_15_1() {
  // (LOW=15, HIGH=1) is the second legal coding of an empty NoteOff bitfield.
  auto bin = hex_to_bin("80 F1");
  io_bytes_reader reader(bin);
  auto decoded = journal_codec_t::read_chapter_n(reader);
  ASSERT_FALSE(decoded.has_note_off());
  ASSERT_EQUAL(decoded.note_logs.size(), 0);
}

void test_chapter_n_rejects_bad_low_high() {
  // LOW > HIGH is only valid for (15,0) and (15,1).
  auto bin = hex_to_bin("80 31");
  io_bytes_reader reader(bin);
  bool thrown = false;
  try {
    journal_codec_t::read_chapter_n(reader);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_chapter_n_reads_truncated() {
  // One note log announced, none present.
  auto bin = hex_to_bin("81 F0");
  io_bytes_reader reader(bin);
  bool thrown = false;
  try {
    journal_codec_t::read_chapter_n(reader);
  } catch (const std::exception &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

// ── Whole journal ───────────────────────────────────────────────────────

void test_write_journal_n_empty() {
  io_bytes_writer_static<64> writer;
  journal_codec_t::write_journal_n(writer, 0x0042, true, {});
  // A=0: the 3 octet empty journal.
  assert_written(writer, "80 00 42");
}

void test_write_journal_n_one_channel() {
  journal_channel_t channel;
  channel.channel = 0;
  // A note log (NoteOn side) and a NoteOff bit for a different note: a note
  // number MUST NOT appear in both structures.
  channel.chapter_n.note_logs.push_back(
      journal_note_log_t{0x48, 0x7F, true, true});
  channel.chapter_n.set_note_off(0x4A);

  io_bytes_writer_static<64> writer;
  journal_codec_t::write_journal_n(writer, 0x0002, true, {channel});
  // header: S=1 A=1 TOTCHAN=0 -> 0xA0, checkpoint 2
  // channel: S=1 chan 0 LENGTH=8 (3 header + 5 chapter) -> 0x80 0x08, TOC=N
  // chapter: B=1, LEN=1, LOW=HIGH=9, log S=1 48/7F, offbits bit 2 (note 74)
  assert_written(writer, "A0 00 02 | 80 08 08 | 81 99 C8 FF 20");
  ASSERT_EQUAL(writer.pos(),
               journal_codec_t::header_size +
                   journal_codec_t::channel_journal_size(channel.chapter_n));

  io_bytes_reader reader(writer);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.header.checkpoint, 2);
  ASSERT_EQUAL(message.header.a, true);
  ASSERT_EQUAL(message.header.channel_count(), 1);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_EQUAL(message.channels[0].channel, 0);
  ASSERT_TRUE(message.channels[0].has_chapter_n);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs.size(), 1);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs[0].note, 0x48);
  ASSERT_FALSE(message.channels[0].chapter_n.code_note_off(0x48));
  ASSERT_TRUE(message.channels[0].chapter_n.code_note_off(0x4A));
  ASSERT_FALSE(message.has_other_chapters);
  ASSERT_FALSE(message.malformed);
  ASSERT_EQUAL(reader.pos(), writer.pos());
}

void test_write_journal_n_rejects_descending_channels() {
  journal_channel_t a;
  a.channel = 5;
  a.chapter_n.set_note_off(1);
  journal_channel_t b;
  b.channel = 4;
  b.chapter_n.set_note_off(1);
  io_bytes_writer_static<64> writer;
  bool thrown = false;
  try {
    journal_codec_t::write_journal_n(writer, 0, true, {a, b});
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

void test_write_journal_n_derives_top_level_s() {
  journal_channel_t ok;
  ok.channel = 0;
  ok.chapter_n.note_logs.push_back(journal_note_log_t{1, 0x40, true, true});

  // Everything S=1: top-level S stays 1.
  {
    io_bytes_writer_static<64> writer;
    journal_codec_t::write_journal_n(writer, 0, true, {ok});
    ASSERT_EQUAL(writer.start[0] & 0x80, 0x80);
  }
  // A note log with S=0 must clear the top-level S bit.
  {
    journal_channel_t channel = ok;
    channel.chapter_n.note_logs[0].s = false;
    io_bytes_writer_static<64> writer;
    journal_codec_t::write_journal_n(writer, 0, true, {channel});
    ASSERT_EQUAL(writer.start[0] & 0x80, 0x00);
  }
  // A Chapter N B bit of 0 must clear the top-level S bit.
  {
    journal_channel_t channel = ok;
    channel.chapter_n.set_note_off(9);
    channel.chapter_n.b = false;
    io_bytes_writer_static<64> writer;
    journal_codec_t::write_journal_n(writer, 0, true, {channel});
    ASSERT_EQUAL(writer.start[0] & 0x80, 0x00);
  }
  // A channel journal S bit of 0 must clear the top-level S bit.
  {
    journal_channel_t channel = ok;
    channel.s = false;
    io_bytes_writer_static<64> writer;
    journal_codec_t::write_journal_n(writer, 0, true, {channel});
    ASSERT_EQUAL(writer.start[0] & 0x80, 0x00);
  }
}

void test_write_journal_n_skips_empty_channels() {
  journal_channel_t empty;
  empty.channel = 0;
  journal_channel_t full;
  full.channel = 1;
  full.chapter_n.set_note_off(3);

  io_bytes_writer_static<64> writer;
  journal_codec_t::write_journal_n(writer, 0, true, {empty, full});
  // Only channel 1 is coded, so TOTCHAN=0 even though two were passed.
  ASSERT_EQUAL(writer.start[0], 0xA0);
  io_bytes_reader reader(writer);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_EQUAL(message.channels[0].channel, 1);
}

void test_read_journal_empty() {
  auto bin = hex_to_bin("80 00 00");
  io_bytes_reader reader(bin);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.header.a, false);
  ASSERT_EQUAL(message.channels.size(), 0);
  ASSERT_EQUAL(reader.pos(), 3);
}

void test_read_journal_totchan_plus_one() {
  // Two channel journals with TOTCHAN=1: the receiver MUST read 2, not 1.
  journal_channel_t a;
  a.channel = 2;
  a.chapter_n.note_logs.push_back(journal_note_log_t{60, 0x40, true, true});
  journal_channel_t b;
  b.channel = 7;
  b.chapter_n.set_note_off(61);

  io_bytes_writer_static<64> writer;
  journal_codec_t::write_journal_n(writer, 5, true, {a, b});
  ASSERT_EQUAL(writer.start[0] & 0x0F, 1);

  io_bytes_reader reader(writer);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_EQUAL(message.header.channel_count(), 2);
  ASSERT_EQUAL(message.channels.size(), 2);
  ASSERT_EQUAL(message.channels[0].channel, 2);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs.size(), 1);
  ASSERT_EQUAL(message.channels[1].channel, 7);
  ASSERT_TRUE(message.channels[1].chapter_n.code_note_off(61));
  ASSERT_EQUAL(reader.pos(), writer.pos());
}

/**
 * A channel journal with P/C/M/W chapters before Chapter N: the N must still be
 * found by stepping over the earlier chapters using their own formats.
 */
void test_read_journal_skips_chapters_before_n() {
  auto bin = hex_to_bin(
      "A0 00 07 |"    // S=1, A=1, TOTCHAN=0, checkpoint 7
      "98 11 F8 |"    // S=1, chan 3, H=0, LENGTH=17, TOC=P|C|M|W|N
      "8A 00 00 |"    // Chapter P: 3 octets, program 10, no bank
      "80 07 40 |"    // Chapter C: LEN=0 (1 log), controller 7 = 0x40
      "80 02 |"       // Chapter M: no logs, LENGTH=2
      "80 20 |"       // Chapter W: pitch wheel first=0 second=0x20
      "81 F0 48 FF"); // Chapter N: B=1, 1 log, C4 vel 127, no offbits
  io_bytes_reader reader(bin);
  auto message = journal_codec_t::read_journal(reader);

  ASSERT_EQUAL(message.header.checkpoint, 7);
  ASSERT_TRUE(message.has_other_chapters);
  ASSERT_FALSE(message.malformed);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_EQUAL(message.channels[0].channel, 3);
  ASSERT_TRUE(message.channels[0].has_chapter_n);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs.size(), 1);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs[0].note, 0x48);
  ASSERT_EQUAL(message.channels[0].chapter_n.note_logs[0].velocity, 0x7F);
  ASSERT_FALSE(message.channels[0].chapter_n.has_note_off());
  // 3 header + 17 channel journal
  ASSERT_EQUAL(reader.pos(), 20);
}

void test_read_journal_skips_chapters_after_n() {
  // Chapter N followed by T and A bytes: they are skipped using LENGTH.
  auto bin =
      hex_to_bin("A0 00 03 |"
                 "80 08 0A |" // S=1, chan 0, LENGTH=8, TOC=N|T
                 "80 99 90 |" // Chapter N: OFFBITS notes 72 and 75
                 "40 11"); // Chapter T (1 octet) + Chapter A (1 octet) padding
  io_bytes_reader reader(bin);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_TRUE(message.has_other_chapters);
  ASSERT_FALSE(message.malformed);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_TRUE(message.channels[0].chapter_n.code_note_off(72));
  ASSERT_TRUE(message.channels[0].chapter_n.code_note_off(75));
  ASSERT_EQUAL(reader.pos(), bin.size());
}

void test_read_journal_skips_system_journal() {
  // Apple sends a system journal (Y=1) for sequencer state and MTC: it comes
  // before the channel journals and must be stepped over.
  auto bin = hex_to_bin(
      "E0 00 09 |"    // S=1, Y=1, A=1, TOTCHAN=0, checkpoint 9
      "00 04 00 00 |" // System journal: 2 octet header + 2 octets, LENGTH=4
      "80 06 08 |"    // channel 0, LENGTH=6, TOC=N
      "80 99 90");    // Chapter N: OFFBITS notes 72 and 75
  io_bytes_reader reader(bin);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_TRUE(message.has_system_journal);
  ASSERT_EQUAL(message.channels.size(), 1);
  ASSERT_TRUE(message.channels[0].chapter_n.code_note_off(72));
  ASSERT_EQUAL(reader.pos(), bin.size());
}

void test_read_journal_skips_malformed_chapter_n() {
  // Two channel journals; the first has a Chapter N with LOW>HIGH invalid.
  auto bin = hex_to_bin("A1 00 01 |" // TOTCHAN=1: two channel journals
                        "80 05 08 |" // channel 0, LENGTH=5, TOC=N
                        "80 31 |"    // Chapter N with LOW=3, HIGH=1: invalid
                        "80 06 08 |" // channel 1, LENGTH=6, TOC=N
                        "80 99 90");
  io_bytes_reader reader(bin);
  auto message = journal_codec_t::read_journal(reader);
  ASSERT_TRUE(message.malformed);
  ASSERT_EQUAL(message.channels.size(), 2);
  ASSERT_FALSE(message.channels[0].has_chapter_n);
  ASSERT_TRUE(message.channels[1].has_chapter_n);
  ASSERT_TRUE(message.channels[1].chapter_n.code_note_off(72));
  ASSERT_EQUAL(reader.pos(), bin.size());
}

void test_read_journal_rejects_length_overrun() {
  auto bin = hex_to_bin("A0 00 01 | 80 20 08");
  io_bytes_reader reader(bin);
  bool thrown = false;
  try {
    journal_codec_t::read_journal(reader);
  } catch (const bad_journal &) {
    thrown = true;
  }
  ASSERT_TRUE(thrown);
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_header_encode),
      TEST(test_header_roundtrip),
      TEST(test_header_channel_count),
      TEST(test_header_rejects_invalid_totchan),
      TEST(test_channel_header_encode),
      TEST(test_channel_header_roundtrip),
      TEST(test_channel_header_rejects_short_length),
      TEST(test_channel_header_reads_rfc_figure),
      TEST(test_chapter_n_empty),
      TEST(test_chapter_n_note_log),
      TEST(test_chapter_n_s_and_y_bits),
      TEST(test_chapter_n_offbits_one_octet),
      TEST(test_chapter_n_offbits_multi_octet),
      TEST(test_chapter_n_128_note_logs),
      TEST(test_chapter_n_127_note_logs_with_offbits),
      TEST(test_chapter_n_roundtrip),
      TEST(test_chapter_n_rejects_zero_velocity_log),
      TEST(test_chapter_n_rejects_note_in_both_structures),
      TEST(test_chapter_n_rejects_duplicate_note_log),
      TEST(test_chapter_n_rejects_too_many_logs),
      TEST(test_chapter_n_rejects_128_logs_with_offbits),
      TEST(test_chapter_n_reads_empty_offbits_15_1),
      TEST(test_chapter_n_rejects_bad_low_high),
      TEST(test_chapter_n_reads_truncated),
      TEST(test_write_journal_n_empty),
      TEST(test_write_journal_n_one_channel),
      TEST(test_write_journal_n_rejects_descending_channels),
      TEST(test_write_journal_n_derives_top_level_s),
      TEST(test_write_journal_n_skips_empty_channels),
      TEST(test_read_journal_empty),
      TEST(test_read_journal_totchan_plus_one),
      TEST(test_read_journal_skips_chapters_before_n),
      TEST(test_read_journal_skips_chapters_after_n),
      TEST(test_read_journal_skips_system_journal),
      TEST(test_read_journal_skips_malformed_chapter_n),
      TEST(test_read_journal_rejects_length_overrun),
  };

  testcase.run(argc, argv);

  return testcase.exit_code();
}
