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

#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/journal.hpp>

using namespace rtpmidid;
journal_t::journal_t() {
  memset(&channel[0], 0, sizeof(channel));
  has_journal = false;
}

void journal_t::midi_in(uint16_t seqnr, const io_bytes_reader &cmidi_in) {
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
      set_has_journal();
      break;
    }
    case 0x90: {
      DEBUG("JOURNAL NOTE OFF");
      auto ch = command & 0x0F;
      auto note = midi_in.read_uint8() & 0x07F; // Range check
      auto vel = midi_in.read_uint8();
      channel[ch].chapter_n.noteoff_seqn[note] = seqnr;
      channel[ch].chapter_n.noteon_vel[note] = vel;
      set_has_journal();
      break;
    }
    }
  }
}

void journal_t::write_journal(rtpmidid::io_bytes_writer &packet) {
  DEBUG("Write journal!");
}
