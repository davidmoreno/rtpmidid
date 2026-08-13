/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

/// Router actor (spec: midi-routing; design D2/D7/D9).
///
/// The router is the single-writer authority for the connection graph and
/// the MIDI hub: peers send `midi_received` to it; it forwards
/// `midi_to_wire` to destinations. It owns the full peer lifecycle
/// (spawn/join/remove with deadlines and escalation). All handlers are
/// commit-only: O(1) map mutations and mailbox pushes, never I/O.

#pragma once

#include "actor.hpp"
#include "peer_messages.hpp"
#include "router_messages.hpp"
#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

class router_actor_t : public actor_t<data_message_t, router_control_t> {
public:
  using control_messages = router_control_t;
  struct peer_record_t {
    /// Erased mailbox handle: peers and hosted actors (e.g. ALSA) have
    /// different lane types; the router posts data/control through it and
    /// the target validates against its own accepted sets.
    mailbox_handle_t mailbox;
    /// Keep the actor object alive while its (router-owned) thread runs.
    std::shared_ptr<actor_base_t> actor;
    /// Router-owned thread for spawned peers (absent for hosted ids).
    std::optional<std::jthread> thread;
    std::vector<peer_id_t> send_to;
    std::string type;
    std::string meta;
    peer_stats_t stats{0, 0};
  };

  explicit router_actor_t(
      actor_config_t config = actor_config_t{.name = "router",
                                             .scheduling =
                                                 scheduling_class_t::elevated});

  // --- choreography tuning (tests set smaller values) ---
  void set_remove_deadline(std::chrono::milliseconds ms) {
    remove_deadline_ = ms;
  }
  void set_escalation_grace(std::chrono::milliseconds ms) {
    escalation_grace_ = ms;
  }

  /// Read-only registry view (test/status aid; router thread only).
  const std::unordered_map<peer_id_t, peer_record_t> &peers() const {
    return peers_;
  }

protected:
  void on_data(data_message_t &&msg) override;
  void on_control(router_control_t &&msg) override;
  void on_loop() override;

private:
  // control handlers (commit-only)
  void handle_register_peer(register_peer_t &&m);
  void handle_unregister_peer(unregister_peer_t &&m);
  void handle_spawn_peer(spawn_peer_t &&m);
  void handle_remove_peer(remove_peer_t &&m);
  void handle_connect(connect_t &&m);
  void handle_disconnect(disconnect_t &&m);
  void handle_status_req(status_req_t &&m);
  void handle_peer_command(peer_command_t &&m);
  void handle_subscribe_events(subscribe_events_t &&m);
  void handle_unsubscribe_events(unsubscribe_events_t &&m);
  void handle_stop_all(stop_all_t &&m);
  void handle_stopped(stopped_t &&m);
  void handle_actor_died(actor_died_t &&m);

  // helpers
  void forward_midi(data_message_t &&msg);
  void cut_topology(peer_id_t id);
  void erase_peer(peer_id_t id, peer_event_kind_t kind);
  void notify_partner(peer_id_t to, const peer_event_t &ev);
  void notify_subscribers(const peer_event_t &ev);
  template <typename M> void post_to(peer_id_t id, M &&m) {
    auto it = peers_.find(id);
    if (it != peers_.end() && it->second.mailbox) {
      it->second.mailbox.post_control(std::forward<M>(m));
    }
  }
  void complete_pending_remove(peer_id_t id, bool ok, const std::string &error);
  void check_pending_removes();
  void ack(mailbox_handle_t reply_to, const hdr_t &hdr, bool ok,
           const std::string &error = {});

  std::unordered_map<peer_id_t, peer_record_t> peers_;
  std::vector<mailbox_handle_t> subscribers_;
  peer_id_t max_id_ = 1;
  std::chrono::milliseconds remove_deadline_{200};
  std::chrono::milliseconds escalation_grace_{100};

  struct pending_remove_t {
    hdr_t hdr;
    mailbox_handle_t reply_to;
    std::chrono::steady_clock::time_point deadline;
    bool escalated = false;
  };
  std::unordered_map<peer_id_t, pending_remove_t> pending_removes_;
};

} // namespace rtpmididns
