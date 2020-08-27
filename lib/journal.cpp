/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
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

#include <bits/stdint-uintn.h>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/journal.hpp>

using namespace rtpmidid;
journal_t::journal_t() {
  memset(&channel[0], 0, sizeof(channel));
  seq_confirmed = 0;
  seq_sent = 0;
  has_journal = false;
}

void journal_t::midi_in(uint32_t seqnr, const io_bytes_reader &cmidi_in) {
  DEBUG("JOURNAL MIDI IN");

  io_bytes_reader midi_in(cmidi_in);
  midi_in.print_hex();

  while (!midi_in.eof()) {
    auto command = midi_in.read_uint8();
    switch (command & 0xF0) {
    case 0x80: {
      DEBUG("JOURNAL NOTE ON");
      auto ch = command & 0x0F;
      auto note = midi_in.read_uint8() & 0x07F; // Range check
      auto vel = midi_in.read_uint8();
      channel[ch].chapter_n.noteon_seqn[note] = seqnr;
      channel[ch].chapter_n.noteon_vel[note] = vel;
      has_journal = true;
      break;
    }
    case 0x90: {
      DEBUG("JOURNAL NOTE OFF");
      auto ch = command & 0x0F;
      auto note = midi_in.read_uint8() & 0x07F; // Range check
      auto vel = midi_in.read_uint8();
      channel[ch].chapter_n.noteoff_seqn[note] = seqnr;
      channel[ch].chapter_n.noteon_vel[note] = vel;
      has_journal = true;
      break;
    }
    }
  }
  seq_sent = seqnr;
}

void journal_t::write_journal(rtpmidid::io_bytes_writer &packet) {
  // keep header, to overwrite it later
  uint8_t *headerp = packet.position;
  packet.write_uint8(0);
  uint8_t header = 0;

  for (int chan = 0; chan < 16; chan++) {
    if (write_channel_n(chan, packet)) {
      header |= header_chapter_journal_e::N_NOTE_ON_OFF;
    }
  }

  *headerp = header;
}

bool journal_t::write_channel_n(int8_t chan, io_bytes_writer &packet) {
  auto noteon_count = 0;
  auto noteoff_count = 0;
  auto noteoff_low = 127;
  auto noteoff_high = 0;
  auto noteon_seqn = channel[chan].chapter_n.noteon_seqn;
  for (auto noten = 0; noten < 128; noten++) {
    auto seqn = noteon_seqn[noten];
    if (seqn > seq_confirmed) {
      noteon_count++;
    }
  }
  auto noteoff_seqn = channel[chan].chapter_n.noteoff_seqn;
  for (int noten = 0; noten < 127; noten++) {
    auto seqn = noteoff_seqn[noten];
    if (seqn > seq_confirmed) {
      noteoff_count++;
      noteoff_low = std::min(noteoff_low, noten);
      noteoff_high = std::max(noteoff_high, noten);
    }
  }

  if (!noteon_count && !noteoff_count)
    return false;

  auto *header = packet.position;
  packet.write_uint16(0);

  DEBUG("Chapter N. Channel: {}, Has noteon: {}, has note off: {}", chan,
        noteon_count, noteoff_count);

  if (noteon_count) {
    // TODO S bit to 1 always. But called B bit here.
    header[0] = 0x80 | noteon_count;
    auto noteon_vel = channel[chan].chapter_n.noteon_vel;
    for (int noten = 0; noten < 128; noten++) {
      auto seqn = noteon_seqn[noten];
      if (seqn > seq_confirmed) {
        // TODO: Figure out whet is S bit. It looks it must be 1 except in some
        // cases. So always 1
        packet.write_uint8(0x80 | noten);
        // TODO: Encode Y -- Whether to recomend playing or skip this noteon.
        // Now always go for it.
        packet.write_uint8(0x80 | noteon_vel[noten]);
      }
    }
  }
  if (noteoff_count) {
    noteoff_low >>= 4;
    noteoff_high >>= 4;
    // noten is the minimum note to store at the journal
    auto noten = noteoff_low << 4;
    // which is properly in the format needed here.
    header[1] = noten | noteoff_high;
    for (auto notenb = noteoff_low; notenb <= noteoff_high; notenb++) {
      uint8_t bitset = 0;
      for (int i = 0; i < 8; i++) {
        if (noteoff_seqn[noten] > seq_confirmed) {
          bitset |= 1 << i;
        }
        noten++;
      }
      packet.write_uint8(bitset);
    }
  }

  return true;
}

/// Journal parsing

void journal_t::parse_journal(io_bytes_reader &journal_data,
                              signal_t<const io_bytes_reader &> &midi_event) {
  journal_data.print_hex();

  uint8_t header = journal_data.read_uint8();

  // bool S = header & 0x80; // Single packet loss
  // bool Y = header & 0x40; // System journal
  bool A = header & 0x20; // Channel journal
  // bool H = header & 0x10; // Enhanced chapter C encoding
  uint8_t totchan = header & 0x0F;

  uint16_t seqnum = journal_data.read_uint16();

  DEBUG("I got data from seqnum {}. {} channels.", seqnum, totchan);

  if (A) {
    for (auto i = 0; i < totchan; i++) {
      DEBUG("Parse channel pkg {}", i);
      parse_journal_chapter(journal_data, midi_event);
    }
  }
}

void journal_t::parse_journal_chapter(
    io_bytes_reader &journal_data,
    signal_t<const io_bytes_reader &> &midi_event) {
  auto head = journal_data.read_uint8();
  // bool S = head & 0x80;
  // bool H = head & 0x08;

  auto length = ((head & 0x07) << 8) | journal_data.read_uint8();
  auto channel = (head & 0x70) >> 4;
  auto chapters = journal_data.read_uint8();

  DEBUG("Chapters: {:08b}", chapters);

  // Although maybe I dont know how to parse them.. I need to at least skip them
  if (chapters & 0xF0) {
    WARNING("There are some PCMW chapters and I dont even know how to skip "
            "them. Sorry journal invalid.");
    journal_data.skip(length);
  }
  if (chapters & 0x08) {
    parse_journal_chapter_N(channel, journal_data, midi_event);
  }
}

void journal_t::parse_journal_chapter_N(
    uint8_t channel, io_bytes_reader &journal_data,
    signal_t<const io_bytes_reader &> &midi_event) {
  DEBUG("Parse chapter N, channel {}", channel);

  auto curr = journal_data.read_uint8();
  // bool S = head & 0x80;
  auto nnoteon = curr & 0x7f;
  curr = journal_data.read_uint8();
  auto low = (curr >> 4) & 0x0f;
  auto high = curr & 0x0f;

  DEBUG("{} note on count, {} noteoff count", nnoteon, high - low + 1);

  // Prepare some struct, will overwrite mem data and write as midi event
  uint8_t tmp[3];

  for (auto i = 0; i < nnoteon; i++) {
    auto notenum = journal_data.read_uint8();
    auto notevel = journal_data.read_uint8();

    // bool B = (notenum&0x80); // S functionality Appendix A.1

    bool Y =
        (notevel &
         0x80); // If true, must play on, if not better skip, might be stale
    if (Y) {
      tmp[0] = 0x90 | channel;
      tmp[1] = notenum & 0x7f;
      tmp[2] = notevel & 0x7f;
      io_bytes event(tmp, 3);
      midi_event(event);
    }
  }

  tmp[0] = 0x80 | channel;
  for (auto i = low; i <= high; i++) {
    auto bitmap = journal_data.read_uint8();
    auto minnote = i * 8;
    for (auto j = 0; j < 8; j++) {
      if (bitmap & (0x80 >> j)) {
        tmp[1] = minnote;
        tmp[2] = 0;
        io_bytes event(tmp, 3);
        midi_event(event);
      }
    }
  }
}
