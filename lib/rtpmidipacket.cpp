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

#include <rtpmidid/logger.hpp>
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

midi_event_list_t packet_midi_t::get_midi_events() {
  auto size = get_size();
  if (size < 13) {
    DEBUG("MIDI packet to small to contain MIDI data ({} bytes)", size);
    return midi_event_list_t{nullptr, 0};
  }
  auto midi_size = uint32_t(data[12]);
  DEBUG("MIDI SIZE {} bytes", midi_size);
  if (size < midi_size + 12) {
    DEBUG("MIDI packet to small to contain ALL MIDI data ({} bytes)",
          midi_size);
    return midi_event_list_t{nullptr, 0};
  }

  return midi_event_list_t{data + 13, midi_size};
}

size_t midi_event_t::get_event_size() {
  // guess event size
  auto event_no_channel = data[0] & 0xF0;
  size_t ev_size = 1;

  switch (event_no_channel) {
  case 0x80:
  case 0x90:
    ev_size = 3;
    break;
  default:
    ev_size = 1;
  }

  if (ev_size > size) {
    return size;
  }
  return ev_size;
}

} // namespace rtpmidid
