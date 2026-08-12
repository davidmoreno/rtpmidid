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

/// ALSA actor (design D11; task 5.4): one actor, many ports. Owns the seq
/// fd in its own poller; demuxes incoming seq events by destination port
/// into `midi_received`; writes `midi_to_wire.to` to the selected port
/// (non-blocking, EAGAIN/POLLOUT retry); announces new seq ports to the
/// router via `register_peer`/remove. Its peer ids are hosted ids (the
/// router holds the mailbox handle, the actor is untouched by removal).

#pragma once

#include "actor.hpp"
#include "aseq.hpp"
#include "messages.hpp"
#include "midi_normalizer.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

class alsa_actor_t : public actor_t {
public:
  alsa_actor_t(actor_config_t config, std::string alsa_name,
               std::vector<std::string> announce_names,
               mailbox_handle_t router_mailbox);

  /// Create a hosted port now (before start) or at runtime; returns the
  /// router-assigned peer id via the given reply mailbox (or 0 if not
  /// registered yet).
  uint8_t create_port(const std::string &port_name);

  const std::string &alsa_name() const { return alsa_name_; }
  aseq_t *seq() const { return seq_.get(); }

protected:
  void on_start() override;
  void on_stop() override;
  void on_data(data_message_t &&msg) override;
  void on_control(control_message_t &&msg) override;
  void on_loop() override;

private:
  void register_port(uint8_t seq_port, const std::string &name);
  void unregister_port(uint8_t seq_port);
  void flush_output();

  std::string alsa_name_;
  std::vector<std::string> announce_names_;
  mailbox_handle_t router_mailbox_;
  mididata_to_alsaevents_t mididata_decoder_;
  mididata_to_alsaevents_t mididata_encoder_;
  std::unique_ptr<aseq_t> seq_;
  /// router peer id -> seq port
  std::unordered_map<peer_id_t, uint8_t> id_to_port_;
  /// seq port -> router peer id
  std::unordered_map<uint8_t, peer_id_t> port_to_id_;
  struct pending_register_t {
    mailbox_handle_t register_reply; // where the router ack lands
    mailbox_handle_t create_reply;   // who asked for this port (may be null)
  };
  std::unordered_map<uint8_t, pending_register_t> pending_ports_;
  struct port_connections_t {
    rtpmidid::signal_t<snd_seq_event_t *>::connection_t midi;
  };
  std::unordered_map<uint8_t, port_connections_t> port_connections_;
  bool output_pending_ = false;
};

} // namespace rtpmididns
