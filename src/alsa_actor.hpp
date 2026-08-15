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

/// ALSA listener actor (lazy-rtpmidi-connections, design D1/D2/D6): the
/// ALSA-side gateway. One actor owns the seq client and every seq port the
/// daemon creates: waiting ports for outbound remotes (created
/// unregistered), per-connection ports for inbound Network sessions,
/// announced bridge ports, and auto-export subscription ports. Waiting
/// ports register with the router on the first ALSA subscription and
/// unregister on the last unsubscribe (unless a live session is wired);
/// the first/last subscription is reported to the rtpmidi server as a
/// `session_request`, which starts/stops the rtpmidi session (lazy
/// connections). The listener is never a router peer itself. Also owns
/// the `alsa_hw_auto_export` enumeration and port hotplug tracking.

#pragma once

#include "actor.hpp"
#include "alsa_messages.hpp"
#include "aseq.hpp"
#include "midi_normalizer.hpp"
#include "router_messages.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtpmididns {

class alsa_actor_t : public actor_t<data_message_t, alsa_control_t> {
public:
  using control_messages = alsa_control_t;

  /// The kind of a listener-owned seq port.
  enum class port_kind_t : uint8_t {
    announced,      // [alsa_announce] bridge port (registered at create)
    waiting,        // waiting port for a remote (registered on demand)
    per_connection, // created for a live inbound session (registered)
    subscription,   // auto-export subscription port (registered)
  };

  alsa_actor_t(actor_config_t config, std::string alsa_name,
               std::vector<std::string> announce_names,
               std::shared_ptr<router_mailbox_t> router_mailbox,
               std::shared_ptr<server_mailbox_t> server_mailbox = {});

  /// Create a port now (before start) or at runtime. With `waiting`
  /// the port is created unregistered. Returns the seq port number.
  uint8_t create_port(const std::string &port_name, bool waiting = false,
                      const std::string &remote = {});

  const std::string &alsa_name() const { return alsa_name_; }
  aseq_t *seq() const { return seq_.get(); }
  /// The seq port numbers created from `alsa_announce` (for tests).
  std::vector<uint8_t> announced_ports() const;

  // --- introspection for tests ---------------------------------------------
  bool port_registered(uint8_t seq_port) const;
  int port_subscribers(uint8_t seq_port) const;
  port_kind_t port_kind(uint8_t seq_port) const;
  /// Seq port of the waiting port for a remote (false when unknown).
  bool waiting_port_for(const std::string &remote, uint8_t &seq_port) const;
  size_t port_count() const { return ports_.size(); }

protected:
  void on_start() override;
  void on_stop() override;
  void on_data(data_message_t &&msg) override;
  void on_control(alsa_control_t &&msg) override;
  void on_loop() override;

private:
  struct port_state_t {
    uint8_t seq_port = 0;
    std::string name;
    std::string meta;   // target descriptor ("addr:port", "client:port")
    std::string remote; // session identity (waiting ports)
    port_kind_t kind = port_kind_t::announced;
    int subscribers = 0;
    bool registered = false;
    bool register_pending = false;
    bool has_session = false; // live session wired (server-managed)
    peer_id_t router_id = 0;
    /// Replies waiting for the register ack.
    std::vector<std::pair<mailbox_handle_t, hdr_t>> create_replies;
    std::vector<std::pair<mailbox_handle_t, hdr_t>> set_registered_replies;
    /// Session requests queued until the router id is known (subscribed).
    std::vector<bool> pending_session_requests;
    struct connections_t {
      rtpmidid::signal_t<snd_seq_event_t *>::connection_t midi;
      rtpmidid::signal_t<aseq_t::port_t, const std::string &>::connection_t
          subscribe;
      rtpmidid::signal_t<aseq_t::port_t>::connection_t unsubscribe;
    };
    connections_t conns;
    /// ALSA connections of a subscription port to its exported local port
    /// (both directions); released with the port state.
    std::vector<aseq_t::connection_t> local_conns;
  };

  port_state_t &make_port(const std::string &name, port_kind_t kind,
                          const std::string &meta, const std::string &remote);
  void begin_register(port_state_t &p);
  void finish_register(port_state_t &p, peer_id_t id);
  void register_failed(port_state_t &p);
  void unregister_from_router(port_state_t &p);
  void remove_port(uint8_t seq_port);
  void post_session_request(port_state_t &p, bool subscribed);
  void on_subscribe(port_state_t &p, const aseq_t::port_t &other);
  void on_unsubscribe(port_state_t &p, const aseq_t::port_t &other);
  void handle_exports_status(exports_status_req_t &&m);
  bool matches_auto_export(const std::string &name,
                           aseq_t::client_type_e type) const;
  void auto_export_enumerate();
  void auto_export_add(const std::string &client_name,
                       aseq_t::client_type_e type, const aseq_t::port_t &port);
  void auto_export_remove(const aseq_t::port_t &port);
  std::string get_port_name(const aseq_t::port_t &port) const;
  void flush_output();

  std::string alsa_name_;
  std::vector<std::string> announce_names_;
  std::shared_ptr<router_mailbox_t> router_mailbox_;
  std::shared_ptr<server_mailbox_t> server_mailbox_;
  mididata_to_alsaevents_t mididata_decoder_;
  mididata_to_alsaevents_t mididata_encoder_;
  std::unique_ptr<aseq_t> seq_;
  std::unordered_map<uint8_t, port_state_t> ports_;
  /// router peer id -> seq port
  std::unordered_map<peer_id_t, uint8_t> id_to_port_;
  /// seq port -> per-port reply mailbox awaiting the register ack. Kept
  /// separate from `ports_` so an ack for a removed port can still be
  /// answered (the late id gets unregistered).
  std::unordered_map<uint8_t, std::shared_ptr<reply_mailbox_t>>
      pending_registers_;
  /// Auto-exported local ports (client<<8|port) -> export name.
  std::unordered_map<uint16_t, std::string> auto_exports_;
  /// Internal port subscribed to System Announce (port hotplug tracking).
  uint8_t announce_port_ = 0;
  std::vector<aseq_t::connection_t> announce_connections_;
  rtpmidid::signal_t<const std::string &, aseq_t::client_type_e,
                     const aseq_t::port_t &>::connection_t
      added_announcement_conn_;
  rtpmidid::signal_t<const aseq_t::port_t &>::connection_t
      removed_announcement_conn_;
  bool output_pending_ = false;
};

} // namespace rtpmididns
