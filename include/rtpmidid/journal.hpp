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

#pragma once
#include "iobytes.hpp"
#include "rtpmidid/signal.hpp"

namespace rtpmidid {
class journal_t {
public:
  typedef enum {
    S_SINGLE_PACKET_LOSS = 0x80,
    Y_SYSTEM = 0x40,
    A_CHANNEL = 0x20,
    H_ENHANCED_CHAPTER_C = 0x10,
  } header_bits_e;

  typedef enum {
    P_PROGRAM_CHANGE = 0x080,
    C_CONTROL_CHANGE = 0x40,
    M_PARAMETER_SYSTEM = 0x20,
    W_PITCH_WHEEL = 0x10,
    N_NOTE_ON_OFF = 0x08,
    E_NOTE_COMMAND = 0x04,
    T_CHANNEL_AFTERTOUCH = 0x04,
    A_POLY_AFTERTOUCH = 0x01,
  } header_chapter_journal_e;

public:
  bool has_journal;
  uint32_t seq_sent;
  uint32_t seq_confirmed;
  struct {
    struct {
      // Last seq that set that noteoff
      uint32_t noteoff_seqn[128];
      // Last seq that set that noteon
      uint32_t noteon_seqn[128];
      // Last note velocity
      uint8_t noteon_vel[128];
    } chapter_n;
  } channel[16];

  journal_t();
  void midi_in(uint32_t seq_nr, const io_bytes_reader &midi_in);

  // Write journal
  bool write_journal(io_bytes_writer &packet);
  bool write_channel_n(int8_t channel, io_bytes_writer &packet);

  // Parse
  void parse_journal(io_bytes_reader &,
                     signal_t<const io_bytes_reader &> &midi_event);
  void parse_journal_chapter(io_bytes_reader &,
                             signal_t<const io_bytes_reader &> &midi_event);
  void parse_journal_chapter_N(uint8_t channel, io_bytes_reader &,
                               signal_t<const io_bytes_reader &> &midi_event);
};
} // namespace rtpmidid
