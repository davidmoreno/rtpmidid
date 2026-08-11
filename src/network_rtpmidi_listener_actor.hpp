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

/// RTP-MIDI listener as an actor (task 5.3). Owns the accept/control
/// sockets (port P and midi P+1) in its own poller. New connections are
/// prep-and-post: the listener prepares a spawn bundle (initial IN
/// datagram, client address, a pre-created mailbox for routing) and posts
/// `spawn_peer` to the router, then forgets the connection — it never
/// tracks children. Datagrams that land on the accept sockets are routed
/// to the owning peer actor by initiator id / ssrc; misdelivered
/// datagrams re-forwarded by peer actors are routed the same way. Outbound
/// DNS resolution is delegated to the worker (dns_resolved reply).

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include "rtpmidid/networkaddress.hpp"
#include <unordered_map>

namespace rtpmididns {

class network_rtpmidi_listener_actor_t : public actor_t {
public:
  /// `control_port` 0 = ephemeral (midi = control + 1).
  network_rtpmidi_listener_actor_t(actor_config_t config, std::string name,
                                   uint16_t control_port,
                                   mailbox_handle_t router_mailbox);

  uint16_t control_port() const { return control_port_; }
  uint16_t midi_port() const { return control_port_ + 1; }
  size_t connection_count() const { return connection_count_; }

protected:
  void on_start() override;
  void on_stop() override;
  void on_control(control_message_t &&msg) override;
  void on_loop() override {}

private:
  struct udp_socket_t {
    int fd = -1;
    rtpmidid::poller_t::listener_t listener;
  };

  void on_udp_read(udp_port_e port);
  void handle_datagram(udp_port_e port, std::vector<uint8_t> &&data,
                       const rtpmidid::network_address_t &from);
  void route_or_spawn(udp_port_e port, std::vector<uint8_t> &data,
                      const rtpmidid::network_address_t &from);
  void spawn_peer(rtpmidid::network_address_t &&from,
                  std::vector<uint8_t> &&initial_in);
  void handle_peer_ids_result(peer_ids_result_t &&m);
  void handle_peer_gone(udp_peer_gone_t &&m);
  void handle_status_req(peer_status_req_t &&m);
  peer_status_variant_t listener_status();

  std::string name_;
  uint16_t control_port_ = 0;
  mailbox_handle_t router_mailbox_;
  udp_socket_t control_;
  udp_socket_t midi_;
  /// Routing: client initiator id / ssrc -> connection peer mailbox.
  std::unordered_map<uint32_t, mailbox_handle_t> by_initiator_;
  std::unordered_map<uint32_t, mailbox_handle_t> by_ssrc_;
  size_t connection_count_ = 0;
};

} // namespace rtpmididns
