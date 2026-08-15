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

/// RTP-MIDI peer connection as an actor (task 5.2). The actor owns its own
/// UDP sockets (control port P and midi port P+1) registered in its own
/// poller and drives the poller-free `rtpmidid::rtppeer_t` protocol engine:
/// socket recv -> `data_ready`, `send_event` -> sendto, `midi_event` ->
/// `midi_received` to the router, `midi_to_wire` -> rtp encode + sendto.
/// Keepalive/connect/reconnect timers are actor-local.
///
/// Two modes:
/// - initiator (client): resolves DNS on the worker, binds, sends IN, CK
///   handshake, periodic CK0 keepalive, reconnect on failure.
/// - acceptor (server): spawned by the rtpmidi server with the initial IN
///   datagram; binds the shared ports with SO_REUSEPORT; datagrams the
///   kernel hash misdelivers are re-forwarded to the server.

#pragma once

#include "rtpmidi_server_messages.hpp" // server_mailbox_t (foreign-datagram re-route)
#include "peer_actor.hpp"
#include "worker_actor.hpp"
#include "rtpmidid/networkaddress.hpp"
#include "rtpmidid/packet.hpp"
#include "rtpmidid/rtppeer.hpp"
#include <memory>
#include <string>

namespace rtpmididns {

class network_rtpmidi_peer_actor_t : public peer_actor_t {
public:
  using control_messages = peer_control_t;
public:
  /// Acceptor mode: an accepted connection. `initial_datagram` is the IN
  /// packet that opened the connection; `local_control_port` is the
  /// server's control port (midi = +1); the server mailbox receives
  /// misdelivered foreign datagrams for re-routing.
  network_rtpmidi_peer_actor_t(
      actor_config_t config, rtpmidid::network_address_t client_address,
      rtpmidid::packet_t &&initial_datagram, uint16_t local_control_port,
      std::shared_ptr<server_mailbox_t> server_mailbox);

  /// Initiator mode: connect to a remote server. DNS runs on the worker.
  network_rtpmidi_peer_actor_t(actor_config_t config, std::string hostname,
                               std::string port, std::string local_base_port,
                               std::shared_ptr<worker_actor_t> worker);

  ~network_rtpmidi_peer_actor_t() override;

  void on_start() override;
  void on_stop() override;
  void on_control(peer_control_t &&msg) override;
  void send_to_wire(peer_id_t to, peer_id_t from,
                    midi_payload_t &&payload) override;
  peer_status_variant_t status() override;
  std::string get_type() const override { return "network_rtpmidi_peer_t"; }

  bool is_connected() const {
    return peer_ && peer_->status == rtpmidid::rtppeer_t::CONNECTED;
  }
  /// Feed a routed datagram (from the listener) to the protocol engine.
  void feed_routed_datagram(std::vector<uint8_t> &&data, udp_port_e port);

private:
  struct udp_socket_t {
    int fd = -1;
    rtpmidid::network_address_t addr;
    rtpmidid::poller_t::listener_t listener;
  };

  enum class mode_t { initiator, acceptor };
  mode_t mode_ = mode_t::initiator;

  std::shared_ptr<rtpmidid::rtppeer_t> peer_;
  udp_socket_t control_; // port P
  udp_socket_t midi_;    // port P+1
  rtpmidid::network_address_t remote_base_addr_; // the other side's control addr
  uint16_t local_control_port_ = 0;
  std::shared_ptr<server_mailbox_t> server_mailbox_;
  std::shared_ptr<worker_actor_t> worker_;
  std::string hostname_;
  std::string port_str_;
  std::string local_base_port_str_;
  std::vector<std::string> dns_addresses_;
  size_t dns_index_ = 0;
  bool connect_started_ = false;
  std::vector<uint8_t> initial_datagram_; // acceptor: the first IN packet

  rtpmidid::poller_t::timer_t timer_;
  int ck_count_ = 0;

  rtpmidid::rtppeer_t::send_event_t::connection_t send_conn_;
  rtpmidid::rtppeer_t::status_change_event_t::connection_t status_conn_;
  rtpmidid::rtppeer_t::midi_event_t::connection_t midi_conn_;
  rtpmidid::rtppeer_t::ck_event_t::connection_t ck_conn_;

  void setup_peer();
  void initiator_flow();
  void try_next_address();
  void bind_control_socket(const rtpmidid::network_address_t &bind_addr,
                           bool reuse_port);
  void bind_midi_socket(uint16_t port, bool reuse_port);
  void acceptor_start();
  void send_packet(rtpmidid::rtppeer_t::port_e port,
                   const rtpmidid::io_bytes_reader &data);
  void on_udp_read(rtpmidid::rtppeer_t::port_e port);
  bool datagram_is_mine(const rtpmidid::packet_t &pkt) const;
  void forward_foreign(const rtpmidid::packet_t &pkt,
                       const rtpmidid::network_address_t &from,
                       rtpmidid::rtppeer_t::port_e port);
  void on_status_change(rtpmidid::rtppeer_t::status_e st);
  void send_ck();
  void on_ck(float ms);
  void disconnected();
};

} // namespace rtpmididns
