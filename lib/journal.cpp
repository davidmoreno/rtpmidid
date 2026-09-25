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

#include <rtpmidid/journal.hpp>
#include <rtpmidid/logger.hpp>

using namespace rtpmidid;

// Bits of the top-level header (RFC 6295 Figure 8)
static constexpr uint8_t JOURNAL_H_S = 0x80;
static constexpr uint8_t JOURNAL_H_Y = 0x40;
static constexpr uint8_t JOURNAL_H_A = 0x20;
static constexpr uint8_t JOURNAL_H_H = 0x10;

// Bits of the channel journal header (RFC 6295 Figure 9)
static constexpr uint8_t CHANNEL_H_S = 0x80;
static constexpr uint8_t CHANNEL_H_CHANNEL = 0x78;
static constexpr uint8_t CHANNEL_H_H = 0x04;
static constexpr uint8_t CHANNEL_H_LENGTH_HIGH = 0x03;

// Bits of the Chapter N header (RFC 6295 Figure A.6.2)
static constexpr uint8_t CHAPTER_N_B = 0x80;
static constexpr uint8_t CHAPTER_N_LEN = 0x7F;

// Every chapter bit except N: chapters we do not decode.
static constexpr uint8_t JOURNAL_CHAPTER_OTHER = uint8_t(~JOURNAL_CHAPTER_N);

// Bits of a Chapter N note log (RFC 6295 Figure A.6.3)
static constexpr uint8_t NOTE_LOG_S = 0x80;
static constexpr uint8_t NOTE_LOG_Y = 0x80;

// Empty NoteOff bitfield coding: LOW=15, HIGH=0 (RFC 6295 Appendix A.6.1)
static constexpr uint8_t OFFBITS_EMPTY_LOW = 15;
static constexpr uint8_t OFFBITS_EMPTY_HIGH = 0;

static bool all_note_logs_have_s(const journal_chapter_n_t &chapter) {
  for (const auto &log : chapter.note_logs) {
    if (!log.s)
      return false;
  }
  return true;
}

/**
 * Content that cannot be expressed on the wire (or must not be sent) is a
 * sender bug, and the chapter is invalid: fail loudly instead of emitting a
 * journal a conforming receiver would misread.
 */
static void validate_chapter_n(const journal_chapter_n_t &chapter) {
  if (chapter.note_logs.size() > 128) {
    throw bad_journal("Chapter N with {} note logs, maximum is 128",
                      chapter.note_logs.size());
  }
  if (chapter.note_logs.size() >= 128 && chapter.has_note_off()) {
    throw bad_journal(
        "Chapter N with 128 note logs cannot code NoteOff OFFBITS octets");
  }
  std::array<bool, 128> seen{};
  for (const auto &log : chapter.note_logs) {
    if (log.note > 127) {
      throw bad_journal("Chapter N note log with note {}", log.note);
    }
    if (log.velocity == 0) {
      throw bad_journal("Chapter N note log for note {} with velocity 0: a "
                        "zero velocity NoteOn is a NoteOff and MUST be coded "
                        "in the OFFBITS structure",
                        log.note);
    }
    if (log.velocity > 127) {
      throw bad_journal("Chapter N note log for note {} with velocity {}",
                        log.note, log.velocity);
    }
    if (seen[log.note]) {
      throw bad_journal("Chapter N with two note logs for note {}", log.note);
    }
    seen[log.note] = true;
    if (chapter.note_off[log.note]) {
      throw bad_journal("Chapter N note {} is coded in both the note log list "
                        "and the OFFBITS structure",
                        log.note);
    }
  }
}

std::pair<uint8_t, uint8_t>
journal_codec_t::offbits_range(const journal_chapter_n_t &chapter) {
  // 128 note logs leave no room for OFFBITS octets: LEN=127,LOW=15,HIGH=0 is
  // the only coding for 128 logs (RFC 6295 Appendix A.6.1).
  if (chapter.note_logs.size() >= 128) {
    return {OFFBITS_EMPTY_LOW, OFFBITS_EMPTY_HIGH};
  }
  int first = -1;
  int last = -1;
  for (int note = 0; note < 128; note++) {
    if (chapter.note_off[note]) {
      if (first < 0) {
        first = note;
      }
      last = note;
    }
  }
  if (first < 0) {
    return {OFFBITS_EMPTY_LOW, OFFBITS_EMPTY_HIGH};
  }
  return {uint8_t(first / 8), uint8_t(last / 8)};
}

size_t journal_codec_t::chapter_n_size(const journal_chapter_n_t &chapter) {
  size_t size = chapter_n_header_size + chapter.note_logs.size() * 2;
  auto [low, high] = offbits_range(chapter);
  if (low <= high) {
    size += size_t(high - low + 1);
  }
  return size;
}

size_t
journal_codec_t::channel_journal_size(const journal_chapter_n_t &chapter) {
  if (chapter.empty()) {
    return channel_header_size;
  }
  return channel_header_size + chapter_n_size(chapter);
}

void journal_codec_t::write_header(io_bytes_writer &writer,
                                   const journal_header_t &header) {
  if (!header.a && header.totchan != 0) {
    throw bad_journal("Journal header with A=0 and TOTCHAN={}", header.totchan);
  }
  if (header.totchan > 15) {
    throw bad_journal("Journal header with TOTCHAN={}, maximum is 15",
                      header.totchan);
  }
  writer.write_uint8((header.s ? JOURNAL_H_S : 0x00) |
                     (header.y ? JOURNAL_H_Y : 0x00) |
                     (header.a ? JOURNAL_H_A : 0x00) |
                     (header.h ? JOURNAL_H_H : 0x00) | (header.totchan & 0x0F));
  writer.write_uint16(header.checkpoint);
}

journal_header_t journal_codec_t::read_header(io_bytes_reader &reader) {
  journal_header_t header;
  auto b0 = reader.read_uint8();
  header.s = b0 & JOURNAL_H_S;
  header.y = b0 & JOURNAL_H_Y;
  header.a = b0 & JOURNAL_H_A;
  header.h = b0 & JOURNAL_H_H;
  header.totchan = b0 & 0x0F;
  header.checkpoint = reader.read_uint16();
  return header;
}

void journal_codec_t::write_channel_header(
    io_bytes_writer &writer, const journal_channel_header_t &header) {
  if (header.channel > 15) {
    throw bad_journal("Channel journal for channel {}, maximum is 15",
                      header.channel);
  }
  if (header.length < channel_header_size) {
    throw bad_journal("Channel journal LENGTH {} is smaller than its {} octet "
                      "header",
                      header.length, channel_header_size);
  }
  if (header.length > 0x3FF) {
    throw bad_journal("Channel journal LENGTH {} is over the 10 bit maximum",
                      header.length);
  }
  writer.write_uint8((header.s ? CHANNEL_H_S : 0x00) |
                     ((header.channel << 3) & CHANNEL_H_CHANNEL) |
                     (header.h ? CHANNEL_H_H : 0x00) |
                     ((header.length >> 8) & CHANNEL_H_LENGTH_HIGH));
  writer.write_uint8(header.length & 0xFF);
  writer.write_uint8(header.toc);
}

journal_channel_header_t
journal_codec_t::read_channel_header(io_bytes_reader &reader) {
  journal_channel_header_t header;
  auto b0 = reader.read_uint8();
  auto b1 = reader.read_uint8();
  header.s = b0 & CHANNEL_H_S;
  header.channel = (b0 & CHANNEL_H_CHANNEL) >> 3;
  header.h = b0 & CHANNEL_H_H;
  header.length = uint16_t(((b0 & CHANNEL_H_LENGTH_HIGH) << 8) | (b1 & 0xFF));
  header.toc = reader.read_uint8();
  return header;
}

void journal_codec_t::write_chapter_n(io_bytes_writer &writer,
                                      const journal_chapter_n_t &chapter) {
  validate_chapter_n(chapter);

  auto [low, high] = offbits_range(chapter);
  // LEN is 7 bits, so 128 note logs are coded as 127 with an empty OFFBITS
  // structure (RFC 6295 Appendix A.6.1).
  uint8_t len =
      chapter.note_logs.size() == 128 ? 127 : uint8_t(chapter.note_logs.size());

  writer.write_uint8((chapter.b ? CHAPTER_N_B : 0x00) | (len & CHAPTER_N_LEN));
  writer.write_uint8(uint8_t((low << 4) & 0xF0) | (high & 0x0F));

  for (const auto &log : chapter.note_logs) {
    writer.write_uint8((log.s ? NOTE_LOG_S : 0x00) | (log.note & 0x7F));
    writer.write_uint8((log.y ? NOTE_LOG_Y : 0x00) | (log.velocity & 0x7F));
  }

  if (low <= high) {
    // MSB of an OFFBITS octet codes the lowest note number of its group
    // (RFC 6295 Appendix A.6.2).
    for (auto octet = low; octet <= high; octet++) {
      uint8_t bits = 0;
      for (uint8_t bit = 0; bit < 8; bit++) {
        if (chapter.note_off[octet * 8 + bit]) {
          bits |= uint8_t(0x80 >> bit);
        }
      }
      writer.write_uint8(bits);
    }
  }
}

journal_chapter_n_t journal_codec_t::read_chapter_n(io_bytes_reader &reader) {
  journal_chapter_n_t chapter;
  auto b0 = reader.read_uint8();
  auto b1 = reader.read_uint8();
  chapter.b = b0 & CHAPTER_N_B;
  uint8_t len = b0 & CHAPTER_N_LEN;
  uint8_t low = (b1 >> 4) & 0x0F;
  uint8_t high = b1 & 0x0F;

  // LEN=127 with an empty OFFBITS structure codes 128 note logs; any other
  // LEN=127 codes 127 logs.
  uint16_t note_logs = len;
  if (len == 127 && low == OFFBITS_EMPTY_LOW && high == OFFBITS_EMPTY_HIGH) {
    note_logs = 128;
  }
  chapter.note_logs.reserve(note_logs);
  for (uint16_t i = 0; i < note_logs; i++) {
    journal_note_log_t log;
    auto l0 = reader.read_uint8();
    auto l1 = reader.read_uint8();
    log.s = l0 & NOTE_LOG_S;
    log.note = l0 & 0x7F;
    // RFC 6295: VELOCITY is never zero. Accept it anyway (be liberal in what we
    // accept) and let the receiver treat it as the NoteOff it means.
    log.y = l1 & NOTE_LOG_Y;
    log.velocity = l1 & 0x7F;
    chapter.note_logs.push_back(log);
  }

  if (low <= high) {
    for (auto octet = low; octet <= high; octet++) {
      auto bits = reader.read_uint8();
      for (uint8_t bit = 0; bit < 8; bit++) {
        if (bits & (0x80 >> bit)) {
          chapter.note_off[octet * 8 + bit] = true;
        }
      }
    }
  } else if (!(low == OFFBITS_EMPTY_LOW && (high == 0 || high == 1))) {
    // Only (LOW=15, HIGH=0) and (LOW=15, HIGH=1) may code an empty bitfield.
    throw bad_journal("Chapter N with LOW={} HIGH={}", low, high);
  }

  return chapter;
}

void journal_codec_t::write_journal_n(
    io_bytes_writer &writer, uint16_t checkpoint, bool s,
    const std::vector<journal_channel_t> &channels) {
  // A channel journal with no chapters at all is not coded.
  std::vector<const journal_channel_t *> coded;
  for (const auto &channel : channels) {
    if (!channel.chapter_n.empty()) {
      coded.push_back(&channel);
    }
  }
  if (coded.size() > 16) {
    throw bad_journal("Journal with {} channel journals, maximum is 16",
                      coded.size());
  }
  for (size_t i = 1; i < coded.size(); i++) {
    if (coded[i]->channel <= coded[i - 1]->channel) {
      throw bad_journal("Channel journals MUST be in ascending channel order, "
                        "got {} after {}",
                        coded[i]->channel, coded[i - 1]->channel);
    }
  }

  journal_header_t header;
  // An element with S=0 forces every containing element (up to the top-level
  // header) to S=0 too (RFC 6295 Appendix A.1), so derive it instead of
  // trusting the caller.
  header.s = s;
  for (const auto *channel : coded) {
    if (!channel->s ||
        (channel->chapter_n.has_note_off() && !channel->chapter_n.b) ||
        !all_note_logs_have_s(channel->chapter_n)) {
      header.s = false;
    }
  }
  header.a = !coded.empty();
  header.totchan = coded.empty() ? 0 : uint8_t(coded.size() - 1);
  header.checkpoint = checkpoint;
  write_header(writer, header);

  for (const auto *channel : coded) {
    journal_channel_header_t channel_header;
    // An element with S=0 (or a NoteOff bitfield with B=0, which is its S bit)
    // means the channel journal codes a command from packet I-1, so the channel
    // journal S bit MUST be 0 too (RFC 6295 Appendix A.1). Derive it, like the
    // top level one, instead of trusting the caller.
    channel_header.s = channel->s;
    if ((channel->chapter_n.has_note_off() && !channel->chapter_n.b) ||
        !all_note_logs_have_s(channel->chapter_n)) {
      channel_header.s = false;
    }
    channel_header.channel = channel->channel;
    channel_header.length = uint16_t(channel_journal_size(channel->chapter_n));
    channel_header.toc = JOURNAL_CHAPTER_N;
    write_channel_header(writer, channel_header);
    write_chapter_n(writer, channel->chapter_n);
  }
}

journal_message_t journal_codec_t::read_journal(io_bytes_reader &reader) {
  journal_message_t message;
  message.header = read_header(reader);

  // A system journal (chapters D/V/Q/F/X) precedes the channel journals. We do
  // not decode it, but it has a LENGTH, so we can step over it: Apple sends one
  // for sequencer state and MTC.
  if (message.header.y) {
    if (reader.remaining() < 2) {
      throw bad_journal("Truncated system journal header");
    }
    uint16_t length =
        uint16_t(((reader.read_uint8() & 0x03) << 8) | reader.read_uint8());
    if (length < 2) {
      throw bad_journal("System journal with LENGTH {}, minimum is 2", length);
    }
    if (size_t(length - 2) > reader.remaining()) {
      throw bad_journal("System journal LENGTH {} overflows the journal "
                        "({} octets left)",
                        length, reader.remaining());
    }
    reader.skip(length - 2);
    message.has_system_journal = true;
  }

  if (!message.header.a) {
    // Empty journal: nothing to recover.
    return message;
  }

  uint8_t last_channel = 0;
  for (uint8_t i = 0; i < message.header.channel_count(); i++) {
    if (reader.remaining() < channel_header_size) {
      throw bad_journal("Truncated channel journal {} of {}", i + 1,
                        message.header.channel_count());
    }
    auto start_pos = reader.pos();
    auto channel_header = read_channel_header(reader);
    if (channel_header.length < channel_header_size) {
      throw bad_journal("Channel journal LENGTH {} is smaller than its {} "
                        "octet header",
                        channel_header.length, channel_header_size);
    }
    auto end_pos = start_pos + channel_header.length;
    if (end_pos > reader.size()) {
      throw bad_journal("Channel journal LENGTH {} overflows the journal "
                        "({} octets left)",
                        channel_header.length, reader.size() - start_pos);
    }
    if (i && channel_header.channel <= last_channel) {
      WARNING("Channel journals MUST be in ascending channel order, got {} "
              "after {}",
              channel_header.channel, last_channel);
    }
    last_channel = channel_header.channel;

    if (channel_header.toc & JOURNAL_CHAPTER_OTHER) {
      message.has_other_chapters = true;
    }

    if (channel_header.toc & JOURNAL_CHAPTER_N) {
      journal_channel_t channel;
      channel.channel = channel_header.channel;
      channel.s = channel_header.s;
      // Bounded reader: never read past this channel journal, so a malformed
      // Chapter N cannot eat the next channel journal.
      io_bytes_reader chapter_reader(reader.position, end_pos - reader.pos());
      try {
        // Chapters before N, in TOC order. We do not decode them, but we must
        // step over them to reach Chapter N.
        if (channel_header.toc & JOURNAL_CHAPTER_P) {
          chapter_reader.skip(3); // fixed size, RFC 6295 Appendix A.2
        }
        if (channel_header.toc & JOURNAL_CHAPTER_C) {
          // 1 octet header, LEN codes the number of logs minus one.
          auto len = chapter_reader.read_uint8() & 0x7F;
          chapter_reader.skip(2 * (int(len) + 1));
        }
        if (channel_header.toc & JOURNAL_CHAPTER_M) {
          // 2 octet header, LENGTH includes itself.
          auto len = uint16_t(((chapter_reader.read_uint8() & 0x03) << 8) |
                              chapter_reader.read_uint8());
          if (len < 2) {
            throw bad_journal("Chapter M with LENGTH {}, minimum is 2", len);
          }
          chapter_reader.skip(len - 2);
        }
        if (channel_header.toc & JOURNAL_CHAPTER_W) {
          chapter_reader.skip(2); // fixed size, RFC 6295 Appendix A.5
        }
        channel.chapter_n = read_chapter_n(chapter_reader);
        channel.has_chapter_n = true;
      } catch (const std::exception &e) {
        message.malformed = true;
        WARNING("Malformed Chapter N on channel {}, skipping its channel "
                "journal: {}",
                channel_header.channel, e.what());
      }
      // Chapters after N (E/T/A) and any padding are skipped by LENGTH.
      message.channels.push_back(channel);
    }

    reader.seek(end_pos);
  }

  return message;
}

//
// Receiver side: the RJRS (RFC 4696, "Receiving Streams: The Recovery Journal")
//

// SysEx Reset State commands that make earlier note commands not N-active
// (RFC 6295 Appendix A.1): GM/GM2 System Enable/Disable and DLS On/Off.
static bool is_reset_state_sysex(const uint8_t *data, size_t size) {
  if (size && data[0] == 0xF0) {
    data++;
    size--;
  }
  if (size < 5 || data[0] != 0x7E) {
    return false;
  }
  // F0 7E <device> <sub-id1> <sub-id2> F7
  const uint8_t sub_id1 = data[2];
  const uint8_t sub_id2 = data[3];
  if (sub_id1 == 0x09) {
    return sub_id2 == 0x00 || sub_id2 == 0x01 || sub_id2 == 0x03;
  }
  if (sub_id1 == 0x0A) {
    return sub_id2 == 0x01 || sub_id2 == 0x02;
  }
  return false;
}

/// True if `extended_seq` is before `checkpoint`, modulo 2^16.
static bool seq_before(uint32_t extended_seq, uint16_t checkpoint) {
  return int16_t(uint16_t(extended_seq) - checkpoint) < 0;
}

void recovery_journal_t::reset() {
  channels_ = {};
  has_received_packet_ = false;
  last_seq_ = 0;
  extended_seq_ = 0;
  packet_extended_seq_ = 0;
  confirmed_extended_seq_ = 0;
  has_feedback_ = false;
}

void recovery_journal_t::feedback_in(uint32_t extended_seq) {
  confirmed_extended_seq_ = extended_seq;
  has_feedback_ = true;
  stats.feedback_received++;
}

journal_loss_e recovery_journal_t::observe(uint16_t seq_nr) {
  if (!has_received_packet_) {
    // First packet of the stream: there is no history to compare against, and
    // the receiver note state is empty, so its journal is not a repair. RFC
    // 6295 Section 4 asks receivers to treat it as ending a loss event, but
    // with an empty RJRS that would only replay notes from the checkpoint
    // window, which is the ghost note problem in reverse.
    has_received_packet_ = true;
    last_seq_ = seq_nr;
    extended_seq_ = seq_nr;
    packet_extended_seq_ = extended_seq_;
    return journal_loss_e::none;
  }

  auto delta = int16_t(seq_nr - last_seq_);
  if (delta <= 0) {
    // Duplicate or reordered packet. Play it, but never repair from it: RFC
    // 4696 Section 7 requires not taking actions that introduce artifacts.
    stats.out_of_order++;
    return journal_loss_e::none;
  }

  extended_seq_ += uint32_t(delta);
  last_seq_ = seq_nr;
  packet_extended_seq_ = extended_seq_;

  if (delta == 1) {
    return journal_loss_e::none;
  }
  stats.losses++;
  return delta == 2 ? journal_loss_e::single : journal_loss_e::multi;
}

void recovery_journal_t::reset_channel(uint8_t channel) {
  if (channel > 15) {
    return;
  }
  channels_[channel] = channel_state_t{};
}

void recovery_journal_t::reset_all_channels() {
  for (auto &channel : channels_) {
    channel = channel_state_t{};
  }
}

void recovery_journal_t::note_on(uint8_t channel, uint8_t note,
                                 uint8_t velocity, uint32_t timestamp) {
  auto &state = channels_[channel & 0x0F].notes[note & 0x7F];
  state.velocity = velocity & 0x7F;
  state.extended_seq = packet_extended_seq_;
  state.time = timestamp;
}

void recovery_journal_t::note_off(uint8_t channel, uint8_t note,
                                  uint32_t timestamp) {
  auto &state = channels_[channel & 0x0F].notes[note & 0x7F];
  state.velocity = 0;
  state.extended_seq = packet_extended_seq_;
  state.time = timestamp;
}

void recovery_journal_t::midi_played(const io_bytes_reader &events,
                                     uint32_t timestamp) {
  // SysEx reassembled from several packets is emitted without the leading F0
  // but always ends in F7, so a buffer that starts with a data byte and ends
  // with F7 is a complete SysEx message.
  if (events.size() > 1 && events.start[0] < 0x80 &&
      events.start[events.size() - 1] == 0xF7) {
    if (is_reset_state_sysex(events.start, events.size())) {
      reset_all_channels();
    }
    return;
  }

  io_bytes_reader reader(events);
  while (reader.remaining()) {
    auto status = reader.read_uint8();
    if (status < 0x80) {
      // The caller is expected to expand running status before emitting, so an
      // abbreviated message here is unexpected data: skip it.
      continue;
    }
    const uint8_t channel = status & 0x0F;
    switch (status & 0xF0) {
    case 0x80: { // NoteOff: note, release velocity
      auto note = reader.remaining() ? (reader.read_uint8() & 0x7F) : 0;
      if (reader.remaining()) {
        reader.read_uint8();
      }
      note_off(channel, note, timestamp);
      break;
    }
    case 0x90: { // NoteOn: note, velocity. Velocity 0 means NoteOff.
      auto note = reader.remaining() ? (reader.read_uint8() & 0x7F) : 0;
      auto velocity = reader.remaining() ? (reader.read_uint8() & 0x7F) : 0;
      if (velocity == 0) {
        note_off(channel, note, timestamp);
      } else {
        note_on(channel, note, velocity, timestamp);
      }
      break;
    }
    case 0xA0: { // Poly aftertouch: note, pressure
      if (reader.remaining()) {
        reader.read_uint8();
      }
      if (reader.remaining()) {
        reader.read_uint8();
      }
      break;
    }
    case 0xB0: { // Control change: controller, value
      auto controller = reader.remaining() ? (reader.read_uint8() & 0x7F) : 0;
      if (reader.remaining()) {
        reader.read_uint8();
      }
      if (controller >= 123 || controller == 120) {
        // All Notes Off family and All Sound Off: earlier note commands are not
        // N-active any more, and the notes are not sounding.
        reset_channel(channel);
      }
      break;
    }
    case 0xC0: // Program change: program
    case 0xD0: // Channel aftertouch: pressure
      if (reader.remaining()) {
        reader.read_uint8();
      }
      break;
    case 0xE0: // Pitch wheel: two data bytes
      if (reader.remaining()) {
        reader.read_uint8();
      }
      if (reader.remaining()) {
        reader.read_uint8();
      }
      break;
    default: { // 0xF0: system messages, one complete message per emission
      if (status == 0xFF) {
        reset_all_channels(); // System reset is a Reset State command
      } else if (status == 0xF0) {
        auto start = reader.position;
        auto size = reader.remaining();
        while (reader.remaining()) {
          auto byte = reader.read_uint8();
          if (byte == 0xF7) {
            break;
          }
        }
        if (is_reset_state_sysex(start, size)) {
          reset_all_channels();
        }
      }
      return; // Nothing after a system message interests us
    }
    }
  }
}

void recovery_journal_t::emit_note(
    uint8_t status, uint8_t note, uint8_t velocity,
    signal_t<const io_bytes_reader &> &midi_out) {
  std::array<uint8_t, 3> event{status, uint8_t(note & 0x7F),
                               uint8_t(velocity & 0x7F)};
  io_bytes bytes(event.data(), event.size());
  midi_out(bytes);
}

void recovery_journal_t::handle_chapter_n(
    uint8_t channel, const journal_chapter_n_t &chapter, uint16_t checkpoint,
    journal_loss_e loss, uint32_t timestamp,
    signal_t<const io_bytes_reader &> &midi_out) {
  auto &state = channels_[channel & 0x0F].notes;

  // The NoteOff bitfield comes first (RFC 4696 Section 7.2). Its B bit is the
  // S-style bit for this structure.
  if (!(loss == journal_loss_e::single && chapter.b)) {
    for (uint8_t note = 0; note < 128; note++) {
      if (!chapter.note_off[note]) {
        continue;
      }
      if (state[note].velocity == 0) {
        // We already know this note is off: no artifact to repair.
        continue;
      }
      // A NoteOff (or a NoteOff->NoteOn->NoteOff sequence) was lost: end the
      // note now, which is exactly the stuck note this journal exists for.
      emit_note(uint8_t(0x80 | (channel & 0x0F)), note, 0, midi_out);
      note_off(channel, note, timestamp);
      stats.notes_repaired_off++;
    }
  }

  for (const auto &log : chapter.note_logs) {
    if (loss == journal_loss_e::single && log.s) {
      continue; // Not from the lost packet
    }
    const uint8_t note = log.note & 0x7F;
    auto &note_state = state[note];

    if (log.velocity == 0) {
      // RFC 6295 says a note log velocity is never zero (a zero velocity NoteOn
      // is a NoteOff, coded in the OFFBITS structure). Be liberal: treat it as
      // the NoteOff it means.
      WARNING_RATE_LIMIT(30,
                         "Note log with velocity 0 for note {} on channel "
                         "{}, treating it as a NoteOff",
                         note, channel);
      if (note_state.velocity != 0) {
        emit_note(uint8_t(0x80 | (channel & 0x0F)), note, 0, midi_out);
        note_off(channel, note, timestamp);
        stats.notes_repaired_off++;
      }
      continue;
    }

    const bool sounding = note_state.velocity != 0;
    bool lost_note_off_on = false;
    if (sounding) {
      // The three tests of RFC 4696 Section 7.2 for a lost
      // NoteOff->NoteOn sequence.
      if (note_state.velocity != log.velocity) {
        lost_note_off_on = true;
      } else if (seq_before(note_state.extended_seq, checkpoint)) {
        lost_note_off_on = true;
      } else if (log.y &&
                 (timestamp - note_state.time) > note_on_recent_window) {
        lost_note_off_on = true;
      }
    }

    if (!sounding) {
      // A NoteOn (or a NoteOn->NoteOff->NoteOn sequence) was lost.
      if (log.y) {
        emit_note(uint8_t(0x90 | (channel & 0x0F)), note, log.velocity,
                  midi_out);
        stats.notes_repaired_on++;
      } else {
        stats.notes_skipped++;
      }
    } else {
      if (lost_note_off_on) {
        emit_note(uint8_t(0x80 | (channel & 0x0F)), note, 0, midi_out);
        stats.notes_repaired_off++;
        if (log.y) {
          emit_note(uint8_t(0x90 | (channel & 0x0F)), note, log.velocity,
                    midi_out);
          stats.notes_repaired_on++;
        } else {
          stats.notes_skipped++;
        }
      }
    }

    // The state is updated as if the logged NoteOn had executed, whether it was
    // played or skipped.
    note_state.velocity = log.velocity;
    note_state.extended_seq = packet_extended_seq_;
    note_state.time = timestamp;
  }
}

void recovery_journal_t::parse_journal(
    io_bytes_reader &journal, journal_loss_e loss, uint32_t timestamp,
    signal_t<const io_bytes_reader &> &midi_out) {
  if (loss == journal_loss_e::none) {
    // Journals describe the past. If nothing was lost, there is nothing to
    // repair: this is the common case, as peers like Apple's driver set J=1 on
    // every packet.
    return;
  }

  auto message = journal_codec_t::read_journal(journal);
  stats.journals_received++;
  if (message.malformed) {
    stats.malformed++;
  }
  if (!message.header.a) {
    return; // No channel journals: nothing to repair
  }
  if (loss == journal_loss_e::single && message.header.s) {
    // The lost packet had an empty MIDI command list (RFC 4696 Section 7).
    return;
  }

  for (const auto &channel : message.channels) {
    if (loss == journal_loss_e::single && channel.s) {
      // Nothing in this channel journal comes from the lost packet.
      continue;
    }
    if (channel.has_chapter_n) {
      handle_chapter_n(channel.channel, channel.chapter_n,
                       message.header.checkpoint, loss, timestamp, midi_out);
    }
  }
}

bool recovery_journal_t::has_sounding_notes() const {
  return sounding_notes() != 0;
}

size_t recovery_journal_t::sounding_notes() const {
  size_t count = 0;
  for (const auto &channel : channels_) {
    for (const auto &note : channel.notes) {
      if (note.velocity != 0) {
        count++;
      }
    }
  }
  return count;
}
