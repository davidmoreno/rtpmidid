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

#pragma once
#include "exceptions.hpp"
#include "iobytes.hpp"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// RFC 6295 recovery journal, Chapter N (MIDI NoteOff 0x8 / NoteOn 0x9).
//
// This header is the **wire codec** only: stateless encode/decode of the
// recovery journal structures. The session state machine that decides *what*
// to code and *what to repair* is `recovery_journal_t`, added in a later
// phase (see docs/architecture/recovery-journal.md).
//
// All multi-octet values are big-endian, as everywhere in RTP-MIDI.

namespace rtpmidid {

class bad_journal : public ::rtpmidid::exception {
public:
  template <typename... Args>
  bad_journal(FMT::format_string<Args...> what, Args... args)
      : ::rtpmidid::exception("Bad recovery journal: {}",
                              FMT::format(what, std::forward<Args>(args)...)) {}
};

/// Chapter TOC bits of the channel journal header (RFC 6295 Section 5).
enum journal_chapter_e {
  JOURNAL_CHAPTER_P = 0x80, // Program Change (0xC)
  JOURNAL_CHAPTER_C = 0x40, // Control Change (0xB)
  JOURNAL_CHAPTER_M = 0x20, // Parameter System (part of 0xB)
  JOURNAL_CHAPTER_W = 0x10, // Pitch Wheel (0xE)
  JOURNAL_CHAPTER_N = 0x08, // NoteOff (0x8), NoteOn (0x9)
  JOURNAL_CHAPTER_E = 0x04, // Note Command Extras (0x8, 0x9)
  JOURNAL_CHAPTER_T = 0x02, // Channel Aftertouch (0xD)
  JOURNAL_CHAPTER_A = 0x01, // Poly Aftertouch (0xA)
};

/**
 * Top-level recovery journal header, 3 octets (RFC 6295 Figure 8).
 *
 *   |S|Y|A|H|TOTCHAN|   Checkpoint Packet Seqnum    |
 *
 * The journal covers the MIDI command sections of packets C..I-1, where I is
 * the packet carrying it and C is `checkpoint`. The S bit is 1 by default and
 * MUST be 0 when any element of this journal codes a command stored in packet
 * I-1 (in that case every containing element MUST be 0 too).
 */
struct journal_header_t {
  bool s = true;
  bool y = false;          // system journal present (never set by us)
  bool a = false;          // channel journals present
  bool h = false;          // enhanced Chapter C encoding (never set by us)
  uint8_t totchan = 0;     // number of channel journals, minus one
  uint16_t checkpoint = 0; // sequence number of packet C

  /// Number of channel journals that follow (0 when A is 0).
  uint8_t channel_count() const { return a ? uint8_t(totchan + 1) : 0; }
};

/**
 * Channel journal header, 3 octets (RFC 6295 Figure 9).
 *
 *   |S| CHAN  |H|      LENGTH       |  TOC  |
 *
 * `length` is the total size of the channel journal, **including these three
 * header octets** and every chapter (RFC 6295 Appendix A.1). Receivers MUST
 * use it to advance past a channel journal they do not understand.
 */
struct journal_channel_header_t {
  bool s = true;
  uint8_t channel = 0; // 0-15, same encoding as the MIDI status nibble
  bool h = false;
  uint16_t length = 0;
  uint8_t toc = 0; // journal_chapter_e bits
};

/// One Chapter N note log, 2 octets (RFC 6295 Figure A.6.3).
struct journal_note_log_t {
  uint8_t note = 0;     // 0-127
  uint8_t velocity = 1; // 1-127. Zero-velocity NoteOn is a NoteOff, coded in
                        // the OFFBITS structure instead
  bool y = true; // hint: play (true) or skip (false) the recovered NoteOn
  bool s = true; // S bit of this log
};

/**
 * Chapter N content: the list of note logs (NoteOn side) and the NoteOff
 * bitfield (OFFBITS, NoteOff side) for one channel.
 */
struct journal_chapter_n_t {
  bool b = true; // B bit: S-style bit for the OFFBITS structure
  // Note logs, oldest command first (RFC 6295 Appendix A.1 oldest-first rule).
  std::vector<journal_note_log_t> note_logs;
  // note_off[n] true means a set OFFBITS bit for note n: "there may be a
  // NoteOff you never saw".
  std::array<bool, 128> note_off{};

  bool has_note_off() const {
    for (auto v : note_off) {
      if (v)
        return true;
    }
    return false;
  }
  /// A chapter with no note logs and no OFFBITS bits is not worth coding.
  bool empty() const { return note_logs.empty() && !has_note_off(); }

  bool code_note_off(uint8_t note) const { return note_off[note & 0x7F]; }
  void set_note_off(uint8_t note, bool on = true) {
    note_off[note & 0x7F] = on;
  }
  void clear_note_off(uint8_t note) { note_off[note & 0x7F] = false; }
};

/// One channel journal we understood (Chapter N). Other chapters in the same
/// channel journal are skipped, not represented.
struct journal_channel_t {
  uint8_t channel = 0;
  bool s = true;              // channel journal S bit
  bool has_chapter_n = false; // the channel journal had a Chapter N
  journal_chapter_n_t chapter_n;
};

/// A decoded journal section.
struct journal_message_t {
  journal_header_t header;
  /// Channel journals that carried a Chapter N, ascending channel number.
  std::vector<journal_channel_t> channels;
  /// A system journal was present and skipped (Apple sends one for sequencer
  /// state and MTC). We never code one.
  bool has_system_journal = false;
  /// Some channel journal carried chapters we do not implement (P/C/M/W/E/T/A).
  /// They are skipped safely using LENGTH, but repairs they describe are lost.
  bool has_other_chapters = false;
  /// Some Chapter N was malformed and skipped.
  bool malformed = false;
};

/**
 * @short Stateless wire codec for the RFC 6295 recovery journal.
 *
 * Encoding validates its input and throws bad_journal on content that cannot
 * be expressed (or must not be sent, such as a zero-velocity note log).
 * Decoding throws bad_journal on malformed input, except for per-channel
 * Chapter N content inside read_journal(), which is skipped and flagged in
 * journal_message_t::malformed.
 */
class journal_codec_t {
public:
  static constexpr size_t header_size = 3;
  static constexpr size_t channel_header_size = 3;
  static constexpr size_t chapter_n_header_size = 2;

  // ── Sizes, in octets ────────────────────────────────────────────────

  /// Size of the encoded Chapter N (header + note logs + OFFBITS octets).
  static size_t chapter_n_size(const journal_chapter_n_t &);
  /// Size of a channel journal carrying only this Chapter N (3 + chapter).
  static size_t channel_journal_size(const journal_chapter_n_t &);

  /**
   * OFFBITS octet range for a chapter, as coded in the LOW/HIGH header fields.
   *
   * `low > high` means "no OFFBITS octets": the canonical coding of an empty
   * NoteOff bitfield is (LOW=15, HIGH=0). The (15,1) pair is also accepted
   * when decoding.
   */
  static std::pair<uint8_t, uint8_t> offbits_range(const journal_chapter_n_t &);

  // ── Encoding ────────────────────────────────────────────────────────

  static void write_header(io_bytes_writer &, const journal_header_t &);
  static void write_channel_header(io_bytes_writer &,
                                   const journal_channel_header_t &);
  static void write_chapter_n(io_bytes_writer &, const journal_chapter_n_t &);

  /**
   * Encode a complete journal section: top-level header followed by one channel
   * journal with a Chapter N per entry, in the given order (which MUST be
   * ascending channel number). A and TOTCHAN are derived from @a channels.
   *
   * Channels with an empty Chapter N are not coded; if all are empty the result
   * is the 3-octet "empty journal".
   */
  static void write_journal_n(io_bytes_writer &, uint16_t checkpoint, bool s,
                              const std::vector<journal_channel_t> &channels);

  // ── Decoding ────────────────────────────────────────────────────────

  static journal_header_t read_header(io_bytes_reader &);
  static journal_channel_header_t read_channel_header(io_bytes_reader &);
  static journal_chapter_n_t read_chapter_n(io_bytes_reader &);

  /**
   * Decode a complete journal section, following the top-level header, the
   * (TOTCHAN + 1) channel journals and the Chapter N of each.
   *
   * Chapters before N in a channel journal (P/C/M/W) are skipped using their
   * own format, so a Chapter N that shares a channel journal with them is still
   * found. Chapters after N are skipped using the channel journal LENGTH.
   */
  static journal_message_t read_journal(io_bytes_reader &);
};

} // namespace rtpmidid
