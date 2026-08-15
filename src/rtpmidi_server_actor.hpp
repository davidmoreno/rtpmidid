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

/// The rtpmidi export server actor (lazy-rtpmidi-connections, design
/// D1/D3/D4/D5/D7): a single global actor owning every rtpmidi listen
/// socket (one pair per `[rtpmidi_announce]` section and per exported
/// device), the export registry, the mDNS announcements for every listen
/// endpoint, and the session bookkeeping (one session per remote pair,
/// new connection replaces the old one). On every inbound connection it
/// spawns the real acceptor peer (through the router) and wires it to the
/// ALSA side: a waiting/per-connection seq port (generic Network server),
/// the lazily opened rawmidi peer, or the per-connection seq subscription
/// peer (auto-export). It is never a router peer itself.
///
/// Lazy outbound side: the server records `[connect_to]` and discovered
/// targets, creates their waiting ALSA ports through the listener, and on
/// `session_request` (first ALSA subscription) spawns the initiator
/// client; the last unsubscribe tears it down.

#pragma once

#include "actor.hpp"
#include "alsa_messages.hpp"
#include "mdns_messages.hpp"
#include "rtpmidi_server_messages.hpp"
#include "router_messages.hpp"
#include "rtpmidid/networkaddress.hpp"
#include "worker_actor.hpp"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

class rtpmidi_server_actor_t : public actor_t<std::monostate, server_control_t> {
public:
  using control_messages = server_control_t;

  rtpmidi_server_actor_t(actor_config_t config,
                         std::shared_ptr<router_mailbox_t> router_mailbox,
                         std::shared_ptr<alsa_mailbox_t> alsa_mailbox,
                         std::shared_ptr<mdns_mailbox_t> mdns_mailbox,
                         std::shared_ptr<worker_actor_t> worker);

  // --- introspection for tests ---------------------------------------------
  size_t session_count() const { return sessions_.size(); }
  size_t export_count() const { return exports_.size(); }
  bool has_export(const std::string &name) const;
  bool has_remote(const std::string &remote) const;
  /// Live session info for a remote (for tests).
  struct session_info_t {
    peer_id_t peer_id = 0;
    bool inbound = false;
    uint8_t seq_port = 0;
  };
  bool session_for(const std::string &remote, session_info_t &info) const;
  uint16_t export_port(const std::string &name) const;

protected:
  void on_start() override;
  void on_stop() override;
  void on_control(server_control_t &&msg) override;
  void on_loop() override {}

private:
  struct export_entry_t {
    std::string name;
    export_kind_e kind = export_kind_e::network;
    std::string target; // rawmidi device path / seq "client:port"
    uint16_t control_port = 0;
    int control_fd = -1;
    int midi_fd = -1;
    rtpmidid::poller_t::listener_t control_listener;
    rtpmidid::poller_t::listener_t midi_listener;
  };

  struct remote_entry_t {
    std::string hostname;
    std::string port;
    std::string local_udp_port;
    uint8_t seq_port = 0;  // the waiting ALSA port (0 while pending)
    uint64_t corr = 0;
  };

  struct session_entry_t {
    std::string remote;
    peer_id_t peer_id = 0;        // the network peer (initiator/acceptor)
    peer_id_t device_peer_id = 0; // rawmidi / seq subscription peer (0 none)
    export_kind_e kind = export_kind_e::network;
    bool inbound = false;
    uint32_t initiator_id = 0;
    uint32_t ssrc = 0;
    uint8_t seq_port = 0; // wired ALSA port (0 = device-only session)
    std::string export_name;
  };

  /// An inbound connection being established (spawn/create in flight).
  struct accept_link_t {
    std::string remote;
    std::string export_name;
    export_kind_e kind = export_kind_e::network;
    uint64_t spawn_corr = 0;
    uint64_t alsa_corr = 0;
    peer_id_t net_id = 0;
    peer_id_t alsa_id = 0;   // router id of the ALSA-side peer
    uint8_t seq_port = 0;    // listener port (0 for device sessions)
    bool port_preexisting = false; // reused waiting port
    uint32_t initiator_id = 0;
    uint32_t ssrc = 0;
    int device_fd = -1;      // rawmidi: open fd handed to the spawned peer
    uint64_t rawmidi_corr = 0;
  };

  // Socket layer.
  int open_listen_socket(uint16_t port);
  bool add_export(const export_add_t &m);
  void remove_export(const std::string &name);
  void on_udp_read(const std::string &export_name, udp_port_e port);
  void route_or_spawn(export_entry_t &exp, udp_port_e port,
                      std::vector<uint8_t> &data,
                      const rtpmidid::network_address_t &from);
  void spawn_acceptor(export_entry_t &exp, rtpmidid::network_address_t &&from,
                      std::vector<uint8_t> &&initial_in);
  void route_datagram(udp_port_e port, std::vector<uint8_t> data);

  // Session lifecycle.
  void handle_session_request(session_request_t &&m);
  void start_outbound_session(const std::string &remote, uint8_t seq_port,
                              peer_id_t port_id);
  void stop_outbound_session(const std::string &remote);
  void handle_peer_ids_result(peer_ids_result_t &&m);
  void handle_ack(ack_t &&m);
  void handle_peer_gone(udp_peer_gone_t &&m);
  void end_session(session_entry_t &&s, bool notify_listener);
  void wire_accept_link(accept_link_t &link);
  void replace_existing_session(const std::string &remote);

  // Registry management.
  void handle_remote_discovered(server_remote_discovered_t &&m);
  void handle_remote_gone(server_remote_gone_t &&m);
  void handle_connect_to(server_connect_to_t &&m);
  void create_waiting_port(const std::string &remote, const std::string &meta);
  void handle_rawmidi_client(server_rawmidi_client_t &&m);
  void handle_exports_status(exports_status_req_t &&m);

  std::shared_ptr<router_mailbox_t> router_mailbox_;
  std::shared_ptr<alsa_mailbox_t> alsa_mailbox_;
  std::shared_ptr<mdns_mailbox_t> mdns_mailbox_;
  std::shared_ptr<worker_actor_t> worker_;

  std::map<std::string, export_entry_t> exports_;
  std::unordered_map<std::string, remote_entry_t> remotes_;
  std::unordered_map<std::string, session_entry_t> sessions_; // by remote
  std::unordered_map<uint32_t, std::string> session_by_initiator_;
  /// Routing: client initiator id / ssrc -> connection peer mailbox.
  std::unordered_map<uint32_t, mailbox_handle_t> by_initiator_;
  std::unordered_map<uint32_t, mailbox_handle_t> by_ssrc_;
  std::unordered_map<uint32_t, accept_link_t> links_; // by initiator id
  /// Correlation ids of in-flight outbound spawns.
  struct outbound_pending_t {
    std::string remote;
    uint8_t seq_port = 0;
    peer_id_t port_id = 0;
  };
  std::unordered_map<uint64_t, outbound_pending_t> pending_outbound_;
  /// Correlation ids of waiting-port creates (remote name).
  std::unordered_map<uint64_t, std::string> pending_ports_;
  /// Correlation ids of client-mode rawmidi spawns.
  struct rawmidi_client_pending_t {
    std::string name;
    peer_id_t rawmidi_id = 0;
  };
  struct rawmidi_client_target_t {
    std::string hostname;
    std::string remote_udp_port;
    std::string local_udp_port;
    uint64_t spawn_corr = 0;
  };
  std::unordered_map<uint64_t, rawmidi_client_pending_t> pending_rawmidi_;
  /// Second-stage (network client) spawns for client-mode rawmidi.
  std::unordered_map<uint64_t, rawmidi_client_pending_t> pending_rawmidi_client_;
  /// Client-mode rawmidi network targets, keyed by name until the rawmidi
  /// peer spawn acks.
  std::unordered_map<std::string, rawmidi_client_target_t>
      rawmidi_client_targets_;
  uint64_t corr_counter_ = 0;
};

} // namespace rtpmididns
