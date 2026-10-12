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

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace rtpmidid_lite {

/// Maximum number of SysEx data octets per RFC 6295 segment, so a segment and
/// its two status octets fit comfortably in one RTP-MIDI packet.
inline constexpr size_t SYSEX_SEGMENT_BYTES = 256;

/// Sanity cap for a SysEx being reassembled from ALSA fragments.
inline constexpr size_t MAX_SYSEX_BYTES = 1024 * 1024;

/// Split a complete SysEx message (0xF0 ... 0xF7) into RFC 6295 segments and
/// call `emit` once per segment.
///
/// Each emitted segment is a valid MIDI command field: the first is
/// F0 <data> F0, the middle ones are F7 <data> F0 and the last is
/// F7 <data> F7 (RFC 6295, Section 3.1). A message that fits in a single
/// segment is emitted verbatim as F0 <data> F7.
///
/// Returns false, and emits nothing, when the buffer is not a well formed SysEx
/// message or `segment_bytes` is zero.
bool split_sysex(const uint8_t *data, size_t len, size_t segment_bytes,
                 const std::function<void(const uint8_t *, size_t)> &emit);

enum class sysex_feed_result_e {
  STORED,    ///< Fragment stored, the message is not complete yet.
  COMPLETE,  ///< A complete message was emitted.
  DISCARDED, ///< Fragment dropped: stray continuation or message too large.
};

/// Reassemble the SysEx fragments ALSA produces for long messages.
///
/// The first fragment starts with 0xF0, the following ones are raw continuation
/// data and the last one ends with 0xF7. `feed` calls `emit` with the complete
/// F0 ... F7 message. A message that arrives in a single fragment is emitted
/// right away.
class sysex_assembler_t {
public:
  sysex_feed_result_e
  feed(const uint8_t *data, size_t len,
       const std::function<void(const uint8_t *, size_t)> &emit);

  void reset() { buffer_.clear(); }
  size_t size() const { return buffer_.size(); }

private:
  std::vector<uint8_t> buffer_;
};

} // namespace rtpmidid_lite
