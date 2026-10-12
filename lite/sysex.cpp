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

#include <algorithm>
#include <vector>

namespace rtpmidid_lite {

bool split_sysex(const uint8_t *data, size_t len, size_t segment_bytes,
                 const std::function<void(const uint8_t *, size_t)> &emit) {
  if (data == nullptr || len < 2 || segment_bytes == 0)
    return false;
  if (data[0] != 0xF0 || data[len - 1] != 0xF7)
    return false;

  const uint8_t *payload = data + 1;
  const size_t payload_len = len - 2;

  if (payload_len <= segment_bytes) {
    emit(data, len);
    return true;
  }

  std::vector<uint8_t> segment;
  segment.reserve(segment_bytes + 2);

  size_t offset = 0;
  bool first = true;
  while (offset < payload_len) {
    const size_t count = std::min(segment_bytes, payload_len - offset);
    const bool last = offset + count == payload_len;

    segment.clear();
    segment.push_back(first ? 0xF0 : 0xF7);
    segment.insert(segment.end(), payload + offset, payload + offset + count);
    segment.push_back(last ? 0xF7 : 0xF0);

    emit(segment.data(), segment.size());

    offset += count;
    first = false;
  }

  return true;
}

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
