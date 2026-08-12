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

/// Control socket as actors (spec: control-socket-jsondm; tasks 7.1-7.3).
///
/// A control listener actor owns the listening unix socket and spawns one
/// connection actor per accepted client (owning the client fd + mailbox,
/// normal priority). Commands that need actor-system work are posted as
/// typed requests with a correlation id and a `reply_to` mailbox; the
/// connection actor waits with a selective wait under a requester-side
/// deadline and writes the response itself (byte-compatible wire shapes).
/// A stalled client cannot block other connections or the data plane.

#pragma once

#include "actor.hpp"
#include "control_messages.hpp"
#include "local_rawmidi_peer_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "router_actor.hpp"
#include "worker_actor.hpp"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace rtpmididns {

class router_actor_t;

// ---------------------------------------------------------------------------
/// One client connection (7.2/7.3): owns the client fd and mailbox.
// ---------------------------------------------------------------------------
class control_connection_actor_t
    : public actor_t<std::monostate, connection_control_t> {
public:
  using control_messages = connection_control_t;
public:
  control_connection_actor_t(actor_config_t config, int client_fd,
                             std::shared_ptr<router_mailbox_t> router_mailbox,
                             std::shared_ptr<mdns_mailbox_t> mdns_mailbox,
                             std::shared_ptr<worker_actor_t> worker,
                             std::string version,
                             std::chrono::milliseconds request_deadline);

  void on_start() override;
  void on_stop() override;
  void on_control(connection_control_t &&msg) override;

  /// For the listener: the connection ended (client closed).
  void notify_gone();

private:
  struct pending_command_t {
    std::optional<std::string> id;
    std::string method;
  };

  void on_client_data();
  void parse_and_dispatch(const std::string &line);
  void respond(const pending_command_t &cmd, const char *kind,
               std::string_view payload);
  void respond_error(const pending_command_t &cmd, const std::string &msg);

  // command handlers (async: post typed request, wait, write response)
  void cmd_status(const pending_command_t &cmd);
  void cmd_router_connect(const pending_command_t &cmd, std::string_view params);
  void cmd_router_disconnect(const pending_command_t &cmd, std::string_view params);
  void cmd_router_remove(const pending_command_t &cmd, std::string_view params);
  void cmd_connect(const pending_command_t &cmd, std::string_view params);
  void cmd_router_create(const pending_command_t &cmd, std::string_view params);
  void cmd_mdns_remove(const pending_command_t &cmd, std::string_view params);
  void cmd_export_rawmidi(const pending_command_t &cmd, std::string_view params);
  void cmd_help(const pending_command_t &cmd);
  void cmd_peer_command(const pending_command_t &cmd, const std::string &peer_id_str,
                        const std::string &peer_cmd, std::string_view params);

  void gather_status(uint64_t corr);
  void re_park_gather(uint64_t corr);
  void on_gather_step(uint64_t corr, std::optional<connection_control_t> res);
  void finish_gather(const pending_command_t &cmd, uint64_t corr);

  void wait_ack(const pending_command_t &cmd, uint64_t corr,
                const char *success_payload = "\"ok\"");
  void spawn_via_router(const pending_command_t &cmd, uint64_t corr,
                        spawn_peer_t &&sp);

  uint64_t next_corr() { return ++corr_counter_; }

  int client_fd_ = -1;
  rtpmidid::poller_t::listener_t client_listener_;
  std::string read_buffer_;
  std::shared_ptr<router_mailbox_t> router_mailbox_;
  std::shared_ptr<mdns_mailbox_t> mdns_mailbox_;
  std::shared_ptr<worker_actor_t> worker_;
  std::string version_;
  std::chrono::milliseconds request_deadline_{5000};
  uint64_t corr_counter_ = 0;
  bool gone_ = false;
  pending_command_t pending_cmd_;

  // requester-driven status gather state
  struct gather_state_t {
    uint64_t corr = 0;
    std::vector<peer_meta_t> metas;
    std::unordered_map<peer_id_t, peer_status_variant_t> responses;
    std::unordered_set<peer_id_t> expected;
    bool mdns_done = false;
    mdns_status_resp_t mdns;
    bool finished = false;
  };
  gather_state_t gather_;
};

// ---------------------------------------------------------------------------
/// The control listener (7.1): owns the listening socket, spawns one
/// connection actor per accepted client, tracks and joins them.
// ---------------------------------------------------------------------------
class control_listener_actor_t
    : public actor_t<std::monostate, control_listener_control_t> {
public:
  using control_messages = control_listener_control_t;
public:
  control_listener_actor_t(actor_config_t config, std::string socket_path,
                           std::shared_ptr<router_mailbox_t> router_mailbox,
                           std::shared_ptr<mdns_mailbox_t> mdns_mailbox,
                           std::shared_ptr<worker_actor_t> worker,
                           std::string version,
                           std::chrono::milliseconds request_deadline = std::chrono::seconds(5));

  void on_start() override;
  void on_stop() override;
  void on_control(control_listener_control_t &&msg) override;

private:
  void accept_client();

  std::string socket_path_;
  int listen_fd_ = -1;
  rtpmidid::poller_t::listener_t listener_;
  std::shared_ptr<router_mailbox_t> router_mailbox_;
  std::shared_ptr<mdns_mailbox_t> mdns_mailbox_;
  std::shared_ptr<worker_actor_t> worker_;
  std::string version_;
  std::chrono::milliseconds request_deadline_{5000};
  std::vector<std::shared_ptr<control_connection_actor_t>> connections_;
};

} // namespace rtpmididns
