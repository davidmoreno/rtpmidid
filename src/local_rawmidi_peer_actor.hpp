/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

/// Rawmidi peer as an actor (task 5.5): the fd is opened by the caller
/// (preparation) and handed in; the actor registers it in its own poller,
/// demuxes reads through the MIDI normalizer into `midi_received`, and
/// writes `midi_to_wire` to the device. Oversized payloads (sysex floods)
/// use the message heap escape pool.

#pragma once

#include "midi_normalizer.hpp"
#include "peer_actor.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace rtpmididns {

class local_rawmidi_peer_actor_t : public peer_actor_t {
public:
  local_rawmidi_peer_actor_t(actor_config_t config, std::string device,
                             std::string name, int fd);

  void on_start() override;
  void on_stop() override;
  void send_to_wire(peer_id_t to, peer_id_t from,
                    midi_payload_t &&payload) override;
  peer_status_variant_t status() override;
  std::string get_type() const override { return "local_rawmidi_peer_t"; }

  int fd() const { return fd_; }

private:
  void read_midi();

  std::string device_;
  std::string name_;
  int fd_ = -1;
  midi_normalizer_t normalizer_;
  std::array<uint8_t, 4096> buffer_{};
  rtpmidid::poller_t::listener_t fd_listener_;
};

} // namespace rtpmididns
