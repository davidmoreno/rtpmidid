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

#include "network_rtpmidi_peer_actor.hpp"
#include "peer_status_jsondm.hpp"
#include "utils.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace rtpmididns {

// --- construction -----------------------------------------------------------

network_rtpmidi_peer_actor_t::network_rtpmidi_peer_actor_t(
    actor_config_t config, rtpmidid::network_address_t client_address,
    rtpmidid::packet_t &&initial_datagram, uint16_t local_control_port,
    mailbox_handle_t listener_mailbox)
    : peer_actor_t(std::move(config)), mode_(mode_t::acceptor),
      peer_(std::make_shared<rtpmidid::rtppeer_t>(name())),
      remote_base_addr_(std::move(client_address)),
      local_control_port_(local_control_port),
      listener_mailbox_(std::move(listener_mailbox)) {
  peer_->local_ssrc = ::rtpmidid::rand_u32();
  peer_->remote_address = remote_base_addr_.dup();
  setup_peer();
  // The first IN datagram is buffered and fed after the sockets are bound.
  initial_datagram_ = std::vector<uint8_t>(initial_datagram.get_data(),
                                           initial_datagram.get_data() +
                                               initial_datagram.get_size());
}

network_rtpmidi_peer_actor_t::network_rtpmidi_peer_actor_t(
    actor_config_t config, std::string hostname, std::string port,
    std::string local_base_port, std::shared_ptr<worker_actor_t> worker)
    : peer_actor_t(std::move(config)), mode_(mode_t::initiator),
      peer_(std::make_shared<rtpmidid::rtppeer_t>(name())),
      worker_(std::move(worker)), hostname_(std::move(hostname)),
      port_str_(std::move(port)),
      local_base_port_str_(std::move(local_base_port)) {
  peer_->initiator_id = ::rtpmidid::rand_u32();
  peer_->local_ssrc = ::rtpmidid::rand_u32();
  setup_peer();
}

network_rtpmidi_peer_actor_t::~network_rtpmidi_peer_actor_t() {
  send_conn_.disconnect();
  status_conn_.disconnect();
  midi_conn_.disconnect();
  ck_conn_.disconnect();
}

// --- wiring -----------------------------------------------------------------

void network_rtpmidi_peer_actor_t::setup_peer() {
  send_conn_ = peer_->send_event.connect(
      [this](const rtpmidid::io_bytes_reader &data,
             rtpmidid::rtppeer_t::port_e port) {
        try {
          send_packet(port, data);
        } catch (const std::exception &e) {
          ERROR("Peer {}: send failed: {}", name(), e.what());
          peer_->status_change_event(
              rtpmidid::rtppeer_t::status_e::DISCONNECTED_NETWORK_ERROR);
        }
      });

  status_conn_ = peer_->status_change_event.connect(
      [this](rtpmidid::rtppeer_t::status_e st) { on_status_change(st); });

  midi_conn_ = peer_->midi_event.connect(
      [this](const rtpmidid::io_bytes_reader &data) {
        auto payload =
            midi_payload_t::make(data.position, data.end - data.position);
        if (payload) {
          send_to_router(config_.id, std::move(*payload));
        }
      });

  ck_conn_ = peer_->ck_event.connect([this](float ms) { on_ck(ms); });
}

void network_rtpmidi_peer_actor_t::on_start() {
  peer_actor_t::on_start(); // the `registered` gate
  if (mode_ == mode_t::acceptor) {
    acceptor_start();
  } else {
    initiator_flow();
  }
}

// --- sockets ----------------------------------------------------------------

void network_rtpmidi_peer_actor_t::bind_control_socket(
    const rtpmidid::network_address_t &bind_addr, bool reuse_port) {
  const int fd = ::socket(bind_addr.get_aifamily(), SOCK_DGRAM | SOCK_CLOEXEC,
                          0);
  if (fd < 0) {
    throw std::runtime_error(std::string("socket(): ") + strerror(errno));
  }
  int one = 1;
  if (reuse_port) {
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
  }
  auto sockaddr = bind_addr.get_sockaddr();
  if (::bind(fd, sockaddr, bind_addr.get_socklen()) != 0) {
    const std::string err = strerror(errno);
    ::close(fd);
    throw std::runtime_error("bind control " + bind_addr.to_string() + ": " +
                             err);
  }
  control_.fd = fd;
  control_.addr = rtpmidid::network_address_t{fd};
  control_.listener =
      add_fd_in(fd, [this](int) { on_udp_read(rtpmidid::rtppeer_t::CONTROL_PORT); });
}

void network_rtpmidi_peer_actor_t::bind_midi_socket(uint16_t port,
                                                    bool reuse_port) {
  const int fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    throw std::runtime_error(std::string("socket(): ") + strerror(errno));
  }
  int one = 1;
  if (reuse_port) {
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
  }
  rtpmidid::network_address_list_t addrs("::", std::to_string(port));
  auto addr = *addrs.begin();
  auto sockaddr = addr.get_sockaddr();
  if (::bind(fd, sockaddr, addr.get_socklen()) != 0) {
    const std::string err = strerror(errno);
    ::close(fd);
    throw std::runtime_error("bind midi " + std::to_string(port) + ": " + err);
  }
  midi_.fd = fd;
  midi_.addr = rtpmidid::network_address_t{fd};
  midi_.listener =
      add_fd_in(fd, [this](int) { on_udp_read(rtpmidid::rtppeer_t::MIDI_PORT); });
}

void network_rtpmidi_peer_actor_t::send_packet(
    rtpmidid::rtppeer_t::port_e port, const rtpmidid::io_bytes_reader &data) {
  rtpmidid::packet_t packet(data.start, data.size());
  auto &sock = (port == rtpmidid::rtppeer_t::MIDI_PORT) ? midi_ : control_;
  if (sock.fd < 0) {
    return;
  }
  rtpmidid::network_address_t to = remote_base_addr_.dup();
  // Convention: midi port = control port + 1.
  to.set_port(remote_base_addr_.port() +
              (port == rtpmidid::rtppeer_t::MIDI_PORT ? 1 : 0));
  const ssize_t n = ::sendto(sock.fd, packet.get_data(), packet.get_size(), 0,
                             to.get_sockaddr(), to.get_socklen());
  if (n < 0) {
    throw rtpmidid::network_exception(errno);
  }
}

void network_rtpmidi_peer_actor_t::on_udp_read(
    rtpmidid::rtppeer_t::port_e port) {
  auto &sock = (port == rtpmidid::rtppeer_t::MIDI_PORT) ? midi_ : control_;
  uint8_t buf[4096];
  for (;;) {
    struct sockaddr_storage ss {};
    socklen_t sslen = sizeof(ss);
    const ssize_t n =
        ::recvfrom(sock.fd, buf, sizeof(buf), MSG_DONTWAIT,
                   reinterpret_cast<struct sockaddr *>(&ss), &sslen);
    if (n <= 0) {
      break;
    }
    const rtpmidid::network_address_t from =
        rtpmidid::network_address_t::create_const(
            reinterpret_cast<const struct sockaddr *>(&ss), sslen);
    rtpmidid::packet_t pkt(buf, static_cast<size_t>(n));
    if (mode_ == mode_t::acceptor && !datagram_is_mine(pkt)) {
      forward_foreign(pkt, from, port);
      continue;
    }
    peer_->data_ready(
        rtpmidid::io_bytes_reader(buf, static_cast<uint32_t>(n)), port);
  }
}

bool network_rtpmidi_peer_actor_t::datagram_is_mine(
    const rtpmidid::packet_t &pkt) const {
  // Replicates rtpserver_t::find_peer_by_packet demux: IN/OK/NO match by
  // initiator_id (offset 8); CK/RS match by ssrc (offset 4); BY by ssrc
  // (offset 12).
  const uint8_t *start = pkt.get_data();
  if (pkt.get_size() < 4 || start[0] != 0xFF || start[1] != 0xFF) {
    return false;
  }
  const uint16_t command = (uint16_t(start[2]) << 8) + start[3];
  try {
    rtpmidid::io_bytes_reader data(const_cast<uint8_t *>(start),
                                   uint32_t(pkt.get_size()));
    if (command == rtpmidid::rtppeer_t::IN ||
        command == rtpmidid::rtppeer_t::OK ||
        command == rtpmidid::rtppeer_t::NO) {
      data.seek(8);
      return data.read_uint32() == peer_->initiator_id;
    }
    if (command == rtpmidid::rtppeer_t::CK ||
        command == rtpmidid::rtppeer_t::RS) {
      data.seek(4);
      return data.read_uint32() == peer_->remote_ssrc;
    }
    if (command == rtpmidid::rtppeer_t::BY) {
      data.seek(12);
      return data.read_uint32() == peer_->remote_ssrc;
    }
  } catch (const std::exception &) {
    return false;
  }
  return false;
}

void network_rtpmidi_peer_actor_t::forward_foreign(
    const rtpmidid::packet_t &pkt, const rtpmidid::network_address_t &from,
    rtpmidid::rtppeer_t::port_e port) {
  if (!listener_mailbox_) {
    return;
  }
  udp_datagram_t dg;
  dg.port = port == rtpmidid::rtppeer_t::MIDI_PORT ? udp_port_e::midi
                                                   : udp_port_e::control;
  dg.data.assign(pkt.get_data(), pkt.get_data() + pkt.get_size());
  dg.remote_ip = from.ip();
  dg.remote_port = from.port();
  listener_mailbox_->post_control(std::move(dg));
}

void network_rtpmidi_peer_actor_t::feed_routed_datagram(
    std::vector<uint8_t> &&data, udp_port_e port) {
  peer_->data_ready(
      rtpmidid::io_bytes_reader(data.data(), static_cast<uint32_t>(data.size())),
      port == udp_port_e::midi ? rtpmidid::rtppeer_t::MIDI_PORT
                               : rtpmidid::rtppeer_t::CONTROL_PORT);
}

// --- initiator flow ---------------------------------------------------------

void network_rtpmidi_peer_actor_t::initiator_flow() {
  if (connect_started_) {
    return;
  }
  connect_started_ = true;
  if (!worker_) {
    WARNING("Peer {}: no worker for DNS; aborting connect.", name());
    return;
  }
  resolve_dns(*worker_, hostname_, port_str_, mailbox(), 0x1000 + config_.id);
}

void network_rtpmidi_peer_actor_t::try_next_address() {
  while (dns_index_ < dns_addresses_.size()) {
    const auto ip = dns_addresses_[dns_index_++];
    rtpmidid::network_address_t remote;
    try {
      rtpmidid::network_address_list_t addrs(ip, port_str_);
      remote = (*addrs.begin()).dup();
    } catch (const std::exception &) {
      continue;
    }
    INFO("Peer {}: trying {}:{}", name(), ip, port_str_);
    try {
      remote_base_addr_ = remote.dup();
      peer_->remote_address = remote.dup();
      // Bind control on the configured local base port (0 = ephemeral).
      rtpmidid::network_address_list_t local("::", local_base_port_str_);
      bind_control_socket(*local.begin(), false);
    } catch (const std::exception &e) {
      WARNING("Peer {}: could not bind/connect to {}: {}", name(), ip,
              e.what());
      continue;
    }
    const auto base = control_.addr.port();
    try {
      bind_midi_socket(base + 1, false);
    } catch (const std::exception &e) {
      WARNING("Peer {}: could not bind midi port {}: {}", name(), base + 1,
              e.what());
      continue;
    }
    // Connect timeout, then IN on both ports.
    timer_ = add_timer(10s, [this] {
      WARNING("Peer {}: connect timeout.", name());
      disconnected();
    });
    peer_->connect_to(rtpmidid::rtppeer_t::CONTROL_PORT);
    peer_->connect_to(rtpmidid::rtppeer_t::MIDI_PORT);
    return;
  }
  WARNING("Peer {}: all addresses failed.", name());
  disconnected();
}

// --- acceptor flow ----------------------------------------------------------

void network_rtpmidi_peer_actor_t::acceptor_start() {
  try {
    // Bind the shared ports (SO_REUSEPORT) so the client's replies and
    // midi traffic can land on this actor's own sockets.
    rtpmidid::network_address_list_t local("::",
                                           std::to_string(local_control_port_));
    bind_control_socket(*local.begin(), true);
    bind_midi_socket(local_control_port_ + 1, true);
    peer_->local_address = control_.addr.dup();
  } catch (const std::exception &e) {
    ERROR("Peer {}: cannot bind accepted connection: {}", name(), e.what());
    disconnected();
    return;
  }
  // Feed the initial IN datagram: rtppeer replies OK and transitions to
  // CONTROL_CONNECTED; the client's CK0 completes the handshake.
  if (!initial_datagram_.empty()) {
    feed_routed_datagram(std::move(initial_datagram_), udp_port_e::control);
  }
  // If the client never completes the midi connection, give up.
  timer_ = add_timer(5s, [this] {
    if (peer_->status == rtpmidid::rtppeer_t::status_e::CONTROL_CONNECTED) {
      WARNING("Peer {}: timeout waiting for midi connection.", name());
      peer_->disconnect();
    }
  });
}

// --- status / keepalive -----------------------------------------------------

void network_rtpmidi_peer_actor_t::on_status_change(
    rtpmidid::rtppeer_t::status_e st) {
  if (st == rtpmidid::rtppeer_t::status_e::CONNECTED) {
    INFO("Peer {}: connected to {}.", name(), remote_base_addr_.to_string());
    timer_.disable();
    ck_count_ = 0;
    if (mode_ == mode_t::initiator) {
      send_ck(); // initiate the CK0 handshake
    } else {
      // Server: drop the connection if the client goes silent.
      timer_ = add_timer(60s, [this] {
        WARNING("Peer {}: CK timeout; disconnecting.", name());
        peer_->disconnect();
      });
    }
  } else if (rtpmidid::rtppeer_t::is_disconnected(st)) {
    disconnected();
  }
}

void network_rtpmidi_peer_actor_t::send_ck() {
  ck_count_++;
  peer_->send_ck0();
  timer_ = add_timer(10s, [this] {
    WARNING("Peer {}: CK timeout.", name());
    peer_->disconnect();
  });
}

void network_rtpmidi_peer_actor_t::on_ck(float ms) {
  (void)ms;
  timer_.disable();
  if (mode_ != mode_t::initiator) {
    // Acceptor: any CK activity keeps the connection alive.
    timer_ = add_timer(60s, [this] {
      WARNING("Peer {}: CK timeout; disconnecting.", name());
      peer_->disconnect();
    });
    return;
  }
  // Client: short-period CK while the latency settles, then long-period.
  if (ck_count_ < 6) {
    timer_ = add_timer(2s, [this] { send_ck(); });
  } else {
    timer_ = add_timer(25s, [this] { send_ck(); });
  }
}

void network_rtpmidi_peer_actor_t::disconnected() {
  timer_.disable();
  if (stopping_) {
    return;
  }
  if (mode_ == mode_t::acceptor) {
    // Self-terminate: post stopped and exit; the router treats it as an
    // implicit remove (the wrapper posts the notice).
    WARNING("Peer {}: connection lost; self-terminating.", name());
    request_stop_token();
    return;
  }
  // Initiator: retry after a reconnect backoff.
  WARNING("Peer {}: disconnected; reconnecting in 30s.", name());
  timer_ = add_timer(30s, [this] {
    dns_index_ = 0;
    connect_started_ = false;
    initiator_flow();
  });
}

// --- data path --------------------------------------------------------------

void network_rtpmidi_peer_actor_t::send_to_wire(peer_id_t to, peer_id_t from,
                                                midi_payload_t &&payload) {
  (void)to;
  (void)from;
  if (!peer_ || peer_->status != rtpmidid::rtppeer_t::status_e::CONNECTED) {
    return;
  }
  rtpmidid::io_bytes_reader reader(payload.data(),
                                   static_cast<uint32_t>(payload.size()));
  peer_->send_midi(reader);
}

void network_rtpmidi_peer_actor_t::on_control(control_message_t &&msg) {
  if (auto *dg = std::get_if<udp_datagram_t>(&msg.v)) {
    feed_routed_datagram(std::move(dg->data), dg->port);
    return;
  }
  if (auto *dns = std::get_if<dns_resolved_t>(&msg.v)) {
    dns_addresses_ = dns->addresses;
    dns_index_ = 0;
    try_next_address();
    return;
  }
  peer_actor_t::on_control(std::move(msg));
}

void network_rtpmidi_peer_actor_t::on_stop() {
  if (listener_mailbox_) {
    listener_mailbox_->post_control(
        udp_peer_gone_t{peer_->initiator_id, peer_->remote_ssrc});
  }
  timer_.disable();
  if (peer_ && peer_->is_connected()) {
    try {
      peer_->send_goodbye(rtpmidid::rtppeer_t::CONTROL_PORT);
      peer_->send_goodbye(rtpmidid::rtppeer_t::MIDI_PORT);
    } catch (const std::exception &e) {
      ERROR("Peer {}: goodbye failed: {}", name(), e.what());
    }
  }
  if (control_.fd >= 0) {
    ::close(control_.fd);
    control_.fd = -1;
  }
  if (midi_.fd >= 0) {
    ::close(midi_.fd);
    midi_.fd = -1;
  }
}

peer_status_variant_t network_rtpmidi_peer_actor_t::status() {
  rtp_peer_status_t s;
  s.name = peer_->remote_name;
  s.peer = peer_status(*peer_);
  return s;
}

} // namespace rtpmididns
