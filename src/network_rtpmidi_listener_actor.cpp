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

#include "network_rtpmidi_listener_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "peer_status_jsondm.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/rtppeer.hpp"
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rtpmididns {

network_rtpmidi_listener_actor_t::network_rtpmidi_listener_actor_t(
    actor_config_t config, std::string name, uint16_t control_port,
    std::shared_ptr<router_mailbox_t> router_mailbox,
    std::shared_ptr<alsa_mailbox_t> alsa_mailbox)
    : actor_t(std::move(config)), name_(std::move(name)),
      control_port_(control_port), router_mailbox_(std::move(router_mailbox)),
      alsa_mailbox_(std::move(alsa_mailbox)) {}

void network_rtpmidi_listener_actor_t::on_start() {
  auto open_socket = [&](uint16_t port) -> int {
    const int fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      ERROR("Listener {}: socket(): {}", name_, strerror(errno));
      return -1;
    }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    rtpmidid::network_address_list_t addrs("::", std::to_string(port));
    auto addr = *addrs.begin();
    if (::bind(fd, addr.get_sockaddr(), addr.get_socklen()) != 0) {
      ERROR("Listener {}: bind port {}: {}", name_, port, strerror(errno));
      ::close(fd);
      return -1;
    }
    return fd;
  };

  if (control_port_ == 0) {
    control_port_ = 0;
  }
  control_.fd = open_socket(control_port_);
  if (control_.fd < 0) {
    return;
  }
  // Learn the ephemeral port if 0 was requested.
  {
    rtpmidid::network_address_t actual{control_.fd};
    control_port_ = actual.port();
  }
  midi_.fd = open_socket(control_port_ + 1);
  if (midi_.fd < 0) {
    ::close(control_.fd);
    control_.fd = -1;
    return;
  }
  control_.listener =
      add_fd_in(control_.fd, [this](int) { on_udp_read(udp_port_e::control); });
  midi_.listener =
      add_fd_in(midi_.fd, [this](int) { on_udp_read(udp_port_e::midi); });
  INFO("Listener {}: listening on control {} midi {}", name_, control_port_,
       control_port_ + 1);
}

void network_rtpmidi_listener_actor_t::on_stop() {
  if (control_.fd >= 0) {
    ::close(control_.fd);
    control_.fd = -1;
  }
  if (midi_.fd >= 0) {
    ::close(midi_.fd);
    midi_.fd = -1;
  }
}

void network_rtpmidi_listener_actor_t::on_udp_read(udp_port_e port) {
  auto &sock = (port == udp_port_e::midi) ? midi_ : control_;
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
    handle_datagram(port, std::vector<uint8_t>(buf, buf + n), from);
  }
}

void network_rtpmidi_listener_actor_t::handle_datagram(
    udp_port_e port, std::vector<uint8_t> &&data,
    const rtpmidid::network_address_t &from) {
  route_or_spawn(port, data, from);
}

void network_rtpmidi_listener_actor_t::route_or_spawn(
    udp_port_e port, std::vector<uint8_t> &data,
    const rtpmidid::network_address_t &from) {
  if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xFF) {
    return; // not an rtpmidi packet
  }
  const uint16_t command = (uint16_t(data[2]) << 8) + data[3];
  try {
    if (command == rtpmidid::rtppeer_t::IN ||
        command == rtpmidid::rtppeer_t::OK ||
        command == rtpmidid::rtppeer_t::NO) {
      rtpmidid::io_bytes_reader r(data.data(), uint32_t(data.size()));
      r.seek(8);
      const uint32_t initiator_id = r.read_uint32();
      if (auto it = by_initiator_.find(initiator_id);
          it != by_initiator_.end() && it->second) {
        it->second.post_control(
            udp_datagram_t{port, data, "", 0});
        return;
      }
      if (command == rtpmidid::rtppeer_t::IN) {
        spawn_peer(from.dup(), std::vector<uint8_t>(data));
      }
      return;
    }
    if (command == rtpmidid::rtppeer_t::CK ||
        command == rtpmidid::rtppeer_t::RS) {
      rtpmidid::io_bytes_reader r(data.data(), uint32_t(data.size()));
      r.seek(4);
      const uint32_t ssrc = r.read_uint32();
      if (auto it = by_ssrc_.find(ssrc); it != by_ssrc_.end() && it->second) {
        it->second.post_control(udp_datagram_t{port, data, "", 0});
      }
      return;
    }
    if (command == rtpmidid::rtppeer_t::BY) {
      rtpmidid::io_bytes_reader r(data.data(), uint32_t(data.size()));
      r.seek(12);
      const uint32_t ssrc = r.read_uint32();
      if (auto it = by_ssrc_.find(ssrc); it != by_ssrc_.end() && it->second) {
        it->second.post_control(udp_datagram_t{port, data, "", 0});
      }
    }
  } catch (const std::exception &) {
    return;
  }
}

void network_rtpmidi_listener_actor_t::spawn_peer(
    rtpmidid::network_address_t &&from, std::vector<uint8_t> &&initial_in) {
  // Prep-and-post (5.3): the listener prepares the bundle and posts
  // spawn_peer to the router, then forgets. The peer mailbox is created
  // here so routing entries exist before the router even spawns (no race).
  auto client_address = std::move(from).dup();
  auto peer_mailbox = std::make_shared<peer_mailbox_t>();

  // Parse the client's initiator id and ssrc from the IN packet for the
  // routing tables.
  uint32_t initiator_id = 0;
  uint32_t client_ssrc = 0;
  try {
    rtpmidid::io_bytes_reader r(initial_in.data(),
                                uint32_t(initial_in.size()));
    r.seek(8);
    initiator_id = r.read_uint32();
    client_ssrc = r.read_uint32();
  } catch (const std::exception &) {
    return;
  }

  // The remote's name from the IN packet (used for its ALSA port).
  std::string remote_name;
  try {
    rtpmidid::io_bytes_reader r(initial_in.data(),
                                uint32_t(initial_in.size()));
    r.seek(16); // signature + cmd + protocol + initiator_id + ssrc
    remote_name = r.read_str0();
  } catch (const std::exception &) {
    remote_name = FMT::format("remote-{}", initiator_id);
  }

  auto &link = links_[initiator_id];
  link.remote_name = remote_name;
  link.spawn_corr = ++corr_counter_;
  link.alsa_corr = ++corr_counter_;

  spawn_peer_t sp;
  sp.hdr = hdr_t{link.spawn_corr};
  sp.reply_to = mailbox();
  sp.type = "network_rtpmidi_peer_t";
  sp.meta = name_;
  const auto name_capture = name_;
  const auto control_port_capture = control_port_;
  const auto listener_mailbox_capture = mailbox();
  sp.factory = [peer_mailbox, initial_in = std::move(initial_in),
                client_address = std::move(client_address), name_capture,
                control_port_capture, listener_mailbox_capture](
                   const mailbox_handle_t &sup, peer_id_t pid) mutable {
    rtpmidid::packet_t pkt(const_cast<uint8_t *>(initial_in.data()),
                           initial_in.size());
    auto peer = std::make_shared<network_rtpmidi_peer_actor_t>(
        actor_config_t{.name = name_capture + "#" + std::to_string(pid),
                       .id = pid,
                       .supervisor_mailbox = sup},
        std::move(client_address), std::move(pkt), control_port_capture,
        listener_mailbox_capture);
    // The listener pre-created this mailbox for datagram routing.
    peer->set_mailbox(peer_mailbox);
    return peer;
  };
  router_mailbox_->post_control(std::move(sp));
  // Register the routing entries immediately (before the ack): datagrams
  // queued meanwhile sit in the peer mailbox until the actor drains it.
  by_initiator_[initiator_id] = peer_mailbox;
  by_ssrc_[client_ssrc] = peer_mailbox;
  connection_count_++;
}

void network_rtpmidi_listener_actor_t::handle_peer_ids_result(
    peer_ids_result_t &&m) {
  for (auto &[initiator_id, link] : links_) {
    if (link.spawn_corr == m.hdr.corr) {
      // Spawn ack: the router owns the peer; link the net id and create
      // the ALSA port for the remote.
      if (m.ids.empty()) {
        links_.erase(initiator_id);
        return;
      }
      link.net_id = m.ids[0];
      if (alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_create_port_t{
            hdr_t{link.alsa_corr}, mailbox(), link.remote_name,
            FMT::format("{}:{}", name_, link.remote_name)});
      }
      return;
    }
    if (link.alsa_corr == m.hdr.corr) {
      // ALSA-create ack: wire the hosted port to the connection peer.
      if (m.ids.empty()) {
        links_.erase(initiator_id);
        return;
      }
      link.alsa_id = m.ids[0];
      if (router_mailbox_ && link.net_id != 0) {
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), link.alsa_id, link.net_id});
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), link.net_id, link.alsa_id});
      }
      return;
    }
  }
}

void network_rtpmidi_listener_actor_t::handle_peer_gone(udp_peer_gone_t &&m) {
  by_initiator_.erase(m.initiator_id);
  by_ssrc_.erase(m.ssrc);
  // The connection ended: also drop its ALSA port.
  auto it = links_.find(m.initiator_id);
  if (it != links_.end() && it->second.alsa_id != 0 && alsa_mailbox_) {
    alsa_mailbox_->post_control(
        alsa_remove_port_t{hdr_t{0}, mailbox(), it->second.alsa_id});
  }
  links_.erase(m.initiator_id);
  if (connection_count_ > 0) {
    connection_count_--;
  }
}

void network_rtpmidi_listener_actor_t::on_control(listener_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, peer_ids_result_t>) {
          handle_peer_ids_result(std::move(m));
        } else if constexpr (std::is_same_v<T, udp_peer_gone_t>) {
          handle_peer_gone(std::move(m));
        } else if constexpr (std::is_same_v<T, peer_status_req_t>) {
          m.reply_to.post_control(
              peer_status_resp_t{m.hdr, m.target, listener_status()});
        }
      },
      msg);
}

peer_status_variant_t network_rtpmidi_listener_actor_t::listener_status() {
  rtp_listener_status_t s;
  s.name = name_;
  s.port = control_port_;
  s.peers.clear();
  return s;
}

} // namespace rtpmididns
