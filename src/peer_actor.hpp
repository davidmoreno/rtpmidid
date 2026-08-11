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

/// Peer actor base (spec: midipeer-typed-interface, midi-routing; tasks
/// 5.1, 5.6). A peer is an actor whose supervisor mailbox is the router:
/// it posts `midi_received`, `stopped` and `actor_died` there and receives
/// `midi_to_wire`, status requests and commands on its own lanes.
///
/// The `registered` gate (design D7): a spawned peer must not process wire
/// traffic until the router posts `registered{ids}`; the peer selective-
/// waits for it under the system control deadline and self-terminates
/// (posts `stopped` and exits) on timeout.

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace rtpmididns {

class peer_actor_t : public actor_t {
public:
  explicit peer_actor_t(actor_config_t config);

  /// 5.6: typed peer status produced on the peer thread (router assigns
  /// the common members when gathering).
  virtual peer_status_variant_t status() = 0;
  virtual std::string get_type() const = 0;
  /// Typed command dispatch: parse `params_json` into typed structs and
  /// return the serialized result (same wire shape as midipeer_t::command).
  virtual std::string command_impl(const std::string &cmd,
                                   std::string_view params_json);

  /// 5.1: data path — send a `midi_to_wire` message to the wire.
  virtual void send_to_wire(peer_id_t to, peer_id_t from,
                            midi_payload_t &&payload) = 0;

  /// Hook for topology events (connect/disconnect notifications, ...).
  virtual void on_peer_event(const peer_event_t &ev) { (void)ev; }

  // accessors
  bool registered() const { return registered_; }
  const std::vector<peer_id_t> &registered_ids() const {
    return registered_ids_;
  }
  /// The router mailbox (the peer's supervisor mailbox; its only topology
  /// knowledge).
  mailbox_handle_t router_mailbox() const { return config_.supervisor_mailbox; }
  void set_registered_timeout(std::chrono::milliseconds ms) {
    registered_timeout_ = ms;
  }

  /// Post received MIDI to the router as `midi_received{from, payload}`.
  void send_to_router(peer_id_t from, midi_payload_t &&payload) {
    if (config_.supervisor_mailbox) {
      config_.supervisor_mailbox->post_data(
          data_message_t::midi_received(from, std::move(payload)));
    }
  }

protected:
  void on_start() override;
  void on_data(data_message_t &&msg) override;
  void on_control(control_message_t &&msg) override;

  bool registered_ = false;
  std::vector<peer_id_t> registered_ids_;
  std::chrono::milliseconds registered_timeout_{2000};
};

} // namespace rtpmididns
