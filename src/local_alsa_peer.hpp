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

#pragma once
#include "aseq.hpp"
#include "midipeer.hpp"
#include "rtpmidid/utils.hpp"
#include <optional>

namespace rtpmididns {
/**
 * @short ALSA port that just receives data and send to another midipeer_t
 */
class local_alsa_peer_t : public midipeer_t {
  NON_COPYABLE_NOR_MOVABLE(local_alsa_peer_t);

public:
  uint8_t port;
  std::shared_ptr<aseq_t> seq;
  std::string name;
  mididata_to_alsaevents_t mididata_encoder;
  mididata_to_alsaevents_t mididata_decoder;

  rtpmidid::connection_t<aseq_t::port_t, const std::string &>
      subscribe_connection;
  rtpmidid::connection_t<aseq_t::port_t> unsubscribe_connection;
  rtpmidid::connection_t<snd_seq_event_t *> midi_connection;

  /** When set, ALSA subscription external client:port → this peer's port (recv). */
  std::optional<aseq_t::connection_t> alsa_source_subscription_;
  int subscribe_src_client_ = -1;
  int subscribe_src_port_ = -1;

  local_alsa_peer_t(const std::string &name, std::shared_ptr<aseq_t> seq,
                    int subscribe_from_client = -1,
                    int subscribe_from_port = -1);
  ~local_alsa_peer_t() override;
  void on_router_attached() override;
  router_peer_row_t status() const override;
  void send_midi(midipeer_id_t from, const mididata_t &) override;
  const char *get_type() const override { return "local_alsa_peer_t"; }

private:
  bool alsa_input_attached_ = false;
  void attach_alsa_input();
};
} // namespace rtpmididns
