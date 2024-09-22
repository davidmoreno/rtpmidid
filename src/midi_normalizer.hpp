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
#pragma once

#include <functional>
#include <stdint.h>
#include <vector>

namespace rtpmidid {
class packet_t;
}

namespace rtpmididns {
class midi_normalizer_t {
public:
  midi_normalizer_t();
  ~midi_normalizer_t();

  /// @brief  Receive an stream of MIDI data and normalize it, one packet at a
  /// time.
  /// @param packet
  /// @param callback
  void
  normalize_stream(const rtpmidid::packet_t &packet,
                   std::function<void(const rtpmidid::packet_t &)> callback);

  void
  parse_midi_byte(const uint8_t byte,
                  std::function<void(const rtpmidid::packet_t &)> callback);

  ssize_t get_size_for_midi_command(uint8_t byte);

protected:
  std::vector<uint8_t> m_buffer;
  ssize_t waiting_for_size = -1; // -1 is unknown, 0 is SysEx (until byte F7),
                                 // 1-3 is the size of the packet
};
} // namespace rtpmididns