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

namespace rtpmidid {
class journal_t {
public:
  journal_t();
  void midi_in(uint16_t seq_nr, const io_bytes_reader &midi_in);
  void journal(io_bytes_writer &packet);
  struct {
    struct {
      // Last seq that set that noteoff
      uint16_t noteoff_seqn[128];
      // Last seq that set that noteon
      uint16_t noteon_seqn[128];
      // Last note velocity
      uint8_t noteon_vel[128];
    } chapter_n;
  } channel[16];
};
} // namespace rtpmidid
