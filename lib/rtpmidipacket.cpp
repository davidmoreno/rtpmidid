/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include <rtpmidid/rtpmidipacket.hpp>

namespace rtpmidid {

packet_type_e packet_t::get_packet_type(const uint8_t *data, size_t size) {
  if (packet_command_t::is_command_packet(data, size)) {
    return COMMAND;
  }
  if (packet_midi_t::is_midi_packet(data, size)) {
    return MIDI;
  }
  return UNKNOWN;
}

packet_type_e packet_t::get_packet_type() const {
  return packet_t::get_packet_type(data, size);
}

} // namespace rtpmidid