/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
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

#include "sysex.hpp"

namespace rtpmidid_lite {

sysex_feed_result_e sysex_assembler_t::feed(
    const uint8_t *data, size_t len,
    const std::function<void(const uint8_t *, size_t)> &emit) {
  if (data == nullptr || len == 0)
    return sysex_feed_result_e::STORED;

  if (buffer_.empty()) {
    // The first fragment must start the message.
    if (data[0] != 0xF0)
      return sysex_feed_result_e::DISCARDED;
  } else if (data[0] == 0xF0) {
    // A previous message was aborted; start over with this fragment.
    buffer_.clear();
  }

  if (buffer_.size() + len > MAX_SYSEX_BYTES) {
    buffer_.clear();
    return sysex_feed_result_e::DISCARDED;
  }

  buffer_.insert(buffer_.end(), data, data + len);

  if (buffer_.back() != 0xF7)
    return sysex_feed_result_e::STORED;

  emit(buffer_.data(), buffer_.size());
  buffer_.clear();
  return sysex_feed_result_e::COMPLETE;
}

} // namespace rtpmidid_lite
