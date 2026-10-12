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

#include "sysex.hpp"

#include <alsa/asoundlib.h>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/signal.hpp>
#include <rtpmidid/utils.hpp>
#include <string>
#include <vector>

namespace rtpmidid_lite {

/**
 * @short One duplex ALSA sequencer port, wired to the poller.
 *
 * Incoming events are decoded to a MIDI byte stream and emitted from
 * `midi_event`; SysEx messages are split into RFC 6295 sized segments. Outgoing
 * MIDI is encoded with the ALSA event codec and sent to every port subscribed
 * to ours.
 */
class alsa_seq_t {
  NON_COPYABLE_NOR_MOVABLE(alsa_seq_t)
public:
  explicit alsa_seq_t(const std::string &name);
  ~alsa_seq_t();

  /// Emitted for every MIDI message read from our port, as a MIDI byte stream.
  rtpmidid::signal_t<const rtpmidid::io_bytes_reader &> midi_event;

  /// Send a MIDI byte stream to every port subscribed to ours.
  void send_midi(const rtpmidid::io_bytes_reader &data);

  int client_id() const { return client_id_; }
  int port_id() const { return port_; }

private:
  void read_ready();
  void handle_event(snd_seq_event_t *ev);
  void decode_sysex(snd_seq_event_t *ev);
  void append_sysex(const uint8_t *data, size_t len);
  void emit_sysex(const uint8_t *data, size_t len);

  std::string name_;
  snd_seq_t *seq_ = nullptr;
  int client_id_ = -1;
  int port_ = -1;
  snd_midi_event_t *codec_ = nullptr;
  std::vector<rtpmidid::poller_t::listener_t> listeners_;
  /// ALSA sends a long SysEx as several raw fragments; reassemble them.
  sysex_assembler_t sysex_assembler_;
  /// Reused for the raw bytes of a SysEx fragment too long for the stack.
  std::vector<uint8_t> sysex_buffer_;
};

} // namespace rtpmidid_lite
