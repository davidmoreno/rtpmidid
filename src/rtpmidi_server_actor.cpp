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

#include "rtpmidi_server_actor.hpp"
#include "local_rawmidi_peer_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "settings.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/rtppeer.hpp"
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rtpmididns {

rtpmidi_server_actor_t::rtpmidi_server_actor_t(
    actor_config_t config, std::shared_ptr<router_mailbox_t> router_mailbox,
    std::shared_ptr<alsa_mailbox_t> alsa_mailbox,
    std::shared_ptr<mdns_mailbox_t> mdns_mailbox,
    std::shared_ptr<worker_actor_t> worker)
    : actor_t(std::move(config)), router_mailbox_(std::move(router_mailbox)),
      alsa_mailbox_(std::move(alsa_mailbox)),
      mdns_mailbox_(std::move(mdns_mailbox)), worker_(std::move(worker)) {}

void rtpmidi_server_actor_t::on_start() {
  // Topology events (peer stopped/died/removed) end the corresponding
  // session; the router pushes them after this subscription.
  if (router_mailbox_) {
    router_mailbox_->post_control(
        subscribe_events_t{hdr_t{0}, mailbox_handle()});
  }
  INFO("rtpmidi server {}: ready.", name());
}

void rtpmidi_server_actor_t::on_stop() {
  if (router_mailbox_) {
    router_mailbox_->post_control(
        unsubscribe_events_t{hdr_t{0}, mailbox_handle()});
  }
  // Close every listen socket (the poller listeners release the fds'
  // registrations). Announcements die with the mdns actor.
  for (auto &[name, exp] : exports_) {
    if (exp.control_fd >= 0) {
      ::close(exp.control_fd);
    }
    if (exp.midi_fd >= 0) {
      ::close(exp.midi_fd);
    }
  }
  exports_.clear();
}

// --- export registry ---------------------------------------------------------

int rtpmidi_server_actor_t::open_listen_socket(uint16_t port) {
  const int fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    ERROR("rtpmidi server {}: socket(): {}", name(), strerror(errno));
    return -1;
  }
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
  rtpmidid::network_address_list_t addrs("::", std::to_string(port));
  auto addr = *addrs.begin();
  if (::bind(fd, addr.get_sockaddr(), addr.get_socklen()) != 0) {
    ERROR("rtpmidi server {}: bind port {}: {}", name(), port,
          strerror(errno));
    ::close(fd);
    return -1;
  }
  return fd;
}

bool rtpmidi_server_actor_t::add_export(const export_add_t &m) {
  if (exports_.count(m.name) != 0) {
    WARNING("rtpmidi server {}: export '{}' already exists.", name(), m.name);
    return false;
  }
  export_entry_t exp;
  exp.name = m.name;
  exp.kind = m.kind;
  exp.target = m.target;
  exp.control_fd = open_listen_socket(m.port);
  if (exp.control_fd < 0) {
    return false;
  }
  // Learn the ephemeral port if 0 was requested.
  {
    rtpmidid::network_address_t actual{exp.control_fd};
    exp.control_port = actual.port();
  }
  exp.midi_fd = open_listen_socket(exp.control_port + 1);
  if (exp.midi_fd < 0) {
    ::close(exp.control_fd);
    return false;
  }
  const std::string export_name = m.name;
  exp.control_listener = add_fd_in(exp.control_fd, [this, export_name](int) {
    on_udp_read(export_name, udp_port_e::control);
  });
  exp.midi_listener = add_fd_in(exp.midi_fd, [this, export_name](int) {
    on_udp_read(export_name, udp_port_e::midi);
  });
  const uint16_t port = exp.control_port;
  exports_[m.name] = std::move(exp);
  INFO("rtpmidi server {}: export '{}' ({}) listening on control {} midi {}.",
       name(), m.name, to_string(m.kind), port, port + 1);
  // Every listen endpoint is announced over mDNS (design D7).
  if (mdns_mailbox_) {
    mdns_mailbox_->post_control(mdns_announce_t{m.name, port});
  }
  return true;
}

void rtpmidi_server_actor_t::remove_export(const std::string &export_name) {
  auto it = exports_.find(export_name);
  if (it == exports_.end()) {
    return;
  }
  INFO("rtpmidi server {}: removing export '{}'.", name(), export_name);
  // End every session served by this export.
  for (auto sit = sessions_.begin(); sit != sessions_.end();) {
    if (sit->second.export_name == export_name) {
      auto s = std::move(sit->second);
      sit = sessions_.erase(sit);
      end_session(std::move(s), true);
    } else {
      ++sit;
    }
  }
  if (mdns_mailbox_) {
    mdns_mailbox_->post_control(
        mdns_unannounce_t{export_name, it->second.control_port});
  }
  if (it->second.control_fd >= 0) {
    ::close(it->second.control_fd);
  }
  if (it->second.midi_fd >= 0) {
    ::close(it->second.midi_fd);
  }
  exports_.erase(it);
}

bool rtpmidi_server_actor_t::has_export(const std::string &n) const {
  return exports_.count(n) != 0;
}

uint16_t rtpmidi_server_actor_t::export_port(const std::string &n) const {
  auto it = exports_.find(n);
  return it == exports_.end() ? 0 : it->second.control_port;
}

bool rtpmidi_server_actor_t::session_for(const std::string &remote,
                                         session_info_t &info) const {
  auto it = sessions_.find(remote);
  if (it == sessions_.end()) {
    return false;
  }
  info.peer_id = it->second.peer_id;
  info.inbound = it->second.inbound;
  info.seq_port = it->second.seq_port;
  return true;
}

// --- datagram layer ------------------------------------------------------------

void rtpmidi_server_actor_t::on_udp_read(const std::string &export_name,
                                         udp_port_e port) {
  auto eit = exports_.find(export_name);
  if (eit == exports_.end()) {
    return;
  }
  auto &exp = eit->second;
  auto &sock_fd = (port == udp_port_e::midi) ? exp.midi_fd : exp.control_fd;
  uint8_t buf[4096];
  for (;;) {
    struct sockaddr_storage ss {};
    socklen_t sslen = sizeof(ss);
    const ssize_t n =
        ::recvfrom(sock_fd, buf, sizeof(buf), MSG_DONTWAIT,
                   reinterpret_cast<struct sockaddr *>(&ss), &sslen);
    if (n <= 0) {
      break;
    }
    const rtpmidid::network_address_t from =
        rtpmidid::network_address_t::create_const(
            reinterpret_cast<const struct sockaddr *>(&ss), sslen);
    std::vector<uint8_t> data(buf, buf + n);
    route_or_spawn(exp, port, data, from);
  }
}

void rtpmidi_server_actor_t::route_or_spawn(
    export_entry_t &exp, udp_port_e port, std::vector<uint8_t> &data,
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
        it->second.post_control(udp_datagram_t{port, data, "", 0});
        return;
      }
      if (command == rtpmidid::rtppeer_t::IN) {
        spawn_acceptor(exp, from.dup(), std::vector<uint8_t>(data));
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

void rtpmidi_server_actor_t::route_datagram(
    udp_port_e port, std::vector<uint8_t> data) {
  // Misdelivered datagrams re-forwarded by peer actors: route them to the
  // owning peer by initiator id / ssrc (same demux as the listen sockets).
  if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xFF) {
    return;
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
        it->second.post_control(udp_datagram_t{port, data, "", 0});
      }
      return;
    }
    rtpmidid::io_bytes_reader r(data.data(), uint32_t(data.size()));
    const bool by = command == rtpmidid::rtppeer_t::BY;
    r.seek(by ? 12 : 4);
    const uint32_t ssrc = r.read_uint32();
    if (auto it = by_ssrc_.find(ssrc); it != by_ssrc_.end() && it->second) {
      it->second.post_control(udp_datagram_t{port, data, "", 0});
    }
  } catch (const std::exception &) {
    return;
  }
}

// --- accept flow (design D8 "Inbound") ----------------------------------------

void rtpmidi_server_actor_t::replace_existing_session(
    const std::string &remote) {
  auto it = sessions_.find(remote);
  if (it == sessions_.end()) {
    return;
  }
  // New connection replaces the old one (one session per remote pair).
  WARNING("rtpmidi server {}: replacing existing session with '{}'.", name(),
          remote);
  auto old = std::move(it->second);
  sessions_.erase(it);
  session_by_initiator_.erase(old.initiator_id);
  by_initiator_.erase(old.initiator_id);
  by_ssrc_.erase(old.ssrc);
  if (old.peer_id != 0 && router_mailbox_) {
    router_mailbox_->post_control(
        remove_peer_t{hdr_t{0}, mailbox_handle(), old.peer_id});
  }
  if (old.device_peer_id != 0 && router_mailbox_) {
    router_mailbox_->post_control(
        remove_peer_t{hdr_t{0}, mailbox_handle(), old.device_peer_id});
  }
  // Note: the ALSA port (if any) is NOT unregistered: the new connection
  // rewires it, so with active subscribers the switch is seamless.
}

void rtpmidi_server_actor_t::spawn_acceptor(
    export_entry_t &exp, rtpmidid::network_address_t &&from,
    std::vector<uint8_t> &&initial_in) {
  // Prep-and-post: prepare the bundle and post spawn_peer to the router,
  // then track the wiring through the ack. The peer mailbox is created
  // here so routing entries exist before the router even spawns (no race).
  auto client_address = std::move(from).dup();
  auto peer_mailbox = std::make_shared<peer_mailbox_t>();

  // Parse the client's initiator id, ssrc and name from the IN packet.
  uint32_t initiator_id = 0;
  uint32_t client_ssrc = 0;
  std::string remote_name;
  try {
    rtpmidid::io_bytes_reader r(initial_in.data(),
                                uint32_t(initial_in.size()));
    r.seek(8);
    initiator_id = r.read_uint32();
    client_ssrc = r.read_uint32();
    r.seek(16); // signature + cmd + protocol + initiator_id + ssrc
    remote_name = r.read_str0();
  } catch (const std::exception &) {
    return;
  }
  if (remote_name.empty()) {
    remote_name = FMT::format("remote-{}", initiator_id);
  }
  if (links_.count(initiator_id) != 0) {
    return; // already establishing this connection
  }

  // One session per remote: an existing session is replaced.
  replace_existing_session(remote_name);

  accept_link_t link;
  link.remote = remote_name;
  link.export_name = exp.name;
  link.kind = exp.kind;
  link.spawn_corr = ++corr_counter_;
  link.alsa_corr = ++corr_counter_;
  link.initiator_id = initiator_id;
  link.ssrc = client_ssrc;

  // Device exports: prepare the device side before posting anything
  // fallible.
  if (exp.kind == export_kind_e::rawmidi) {
    // Deferred open (design D5): the device is opened now, on connection.
    const int fd = ::open(exp.target.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      ERROR("rtpmidi server {}: cannot open rawmidi device '{}' for '{}': "
            "{}; rejecting the connection (export stays registered).",
            name(), exp.target, remote_name, strerror(errno));
      return;
    }
    link.device_fd = fd;
    link.rawmidi_corr = ++corr_counter_;
  }

  auto &stored = links_[initiator_id];
  stored = std::move(link);
  auto &l = stored;

  spawn_peer_t sp;
  sp.hdr = hdr_t{l.spawn_corr};
  sp.reply_to = mailbox();
  sp.type = "network_rtpmidi_peer_t";
  sp.meta = exp.name;
  const auto control_port_capture = exp.control_port;
  auto server_mb = mailbox();
  sp.factory = [peer_mailbox, initial_in = std::move(initial_in),
                client_address = std::move(client_address), remote_name,
                control_port_capture, server_mb](const mailbox_handle_t &sup,
                                                 peer_id_t pid) mutable {
    rtpmidid::packet_t pkt(const_cast<uint8_t *>(initial_in.data()),
                           initial_in.size());
    auto peer = std::make_shared<network_rtpmidi_peer_actor_t>(
        actor_config_t{.name = remote_name + "#" + std::to_string(pid),
                       .scheduling = scheduling_class_t::elevated,
                       .rt_enabled = settings.rt_enable,
                       .rt_priority = settings.rt_priority,
                       .id = pid,
                       .supervisor_mailbox = sup},
        std::move(client_address), std::move(pkt), control_port_capture,
        server_mb);
    // The server pre-created this mailbox for datagram routing.
    peer->set_mailbox(peer_mailbox);
    return peer;
  };
  router_mailbox_->post_control(std::move(sp));
  // Register the routing entries immediately (before the ack): datagrams
  // queued meanwhile sit in the peer mailbox until the actor drains it.
  by_initiator_[initiator_id] = peer_mailbox;
  by_ssrc_[client_ssrc] = peer_mailbox;

  // Request the ALSA/device side depending on the export kind.
  if (exp.kind == export_kind_e::network) {
    auto rit = remotes_.find(remote_name);
    if (rit != remotes_.end() && rit->second.seq_port != 0) {
      // Known remote with a waiting port: reuse it (registered while the
      // inbound session is live).
      l.seq_port = rit->second.seq_port;
      l.port_preexisting = true;
      if (alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_port_set_registered_t{
            hdr_t{l.alsa_corr}, mailbox(), l.seq_port, true});
      }
    } else if (alsa_mailbox_) {
      // Unknown remote: fresh per-connection port named after it.
      alsa_mailbox_->post_control(alsa_create_port_t{
          hdr_t{l.alsa_corr}, mailbox(), remote_name,
          FMT::format("{}:{}", exp.name, remote_name), false, remote_name});
    }
  } else if (exp.kind == export_kind_e::seq) {
    if (alsa_mailbox_) {
      alsa_mailbox_->post_control(alsa_subscribe_port_t{
          hdr_t{l.alsa_corr}, mailbox(),
          FMT::format("{} ({})", exp.name, remote_name), exp.target});
    }
  }
  // rawmidi: the device peer is spawned after the acceptor ack (needs the
  // open fd handed over; see handle_peer_ids_result).
}

void rtpmidi_server_actor_t::wire_accept_link(accept_link_t &link) {
  if (link.net_id == 0 || link.alsa_id == 0 || !router_mailbox_) {
    return;
  }
  router_mailbox_->post_control(
      connect_t{hdr_t{0}, mailbox(), link.alsa_id, link.net_id});
  router_mailbox_->post_control(
      connect_t{hdr_t{0}, mailbox(), link.net_id, link.alsa_id});

  session_entry_t s;
  s.remote = link.remote;
  s.peer_id = link.net_id;
  s.device_peer_id =
      link.kind == export_kind_e::network ? 0 : link.alsa_id;
  s.kind = link.kind;
  s.inbound = true;
  s.initiator_id = link.initiator_id;
  s.ssrc = link.ssrc;
  s.seq_port = link.seq_port;
  s.export_name = link.export_name;
  sessions_[link.remote] = std::move(s);
  session_by_initiator_[link.initiator_id] = link.remote;
  if (link.seq_port != 0 && alsa_mailbox_) {
    alsa_mailbox_->post_control(
        alsa_port_session_t{link.seq_port, true});
  }
  INFO("rtpmidi server {}: session with '{}' wired (net id {} <-> alsa id "
       "{}, export '{}').",
       name(), link.remote, link.net_id, link.alsa_id, link.export_name);
  links_.erase(link.initiator_id);
}

// --- lazy outbound (design D8 "Outbound") --------------------------------------

bool rtpmidi_server_actor_t::has_remote(const std::string &remote) const {
  return remotes_.count(remote) != 0;
}

void rtpmidi_server_actor_t::create_waiting_port(const std::string &remote,
                                                 const std::string &meta) {
  auto &r = remotes_[remote];
  if (r.seq_port != 0 || r.corr != 0) {
    return; // already created / in flight
  }
  r.corr = ++corr_counter_;
  pending_ports_[r.corr] = remote;
  if (alsa_mailbox_) {
    alsa_mailbox_->post_control(alsa_create_port_t{
        hdr_t{r.corr}, mailbox(), remote, meta, true, remote});
  }
}

void rtpmidi_server_actor_t::handle_remote_discovered(
    server_remote_discovered_t &&m) {
  // Target registry only: the mdns actor creates the waiting ALSA port
  // (design D8) and reports its seq port through server_remote_port_t.
  auto &r = remotes_[m.remote];
  r.hostname = m.address;
  r.port = m.port;
}

void rtpmidi_server_actor_t::handle_connect_to(server_connect_to_t &&m) {
  auto &r = remotes_[m.remote];
  r.hostname = m.hostname;
  r.port = m.port;
  r.local_udp_port = m.local_udp_port;
  create_waiting_port(m.remote, FMT::format("{}:{}", m.hostname, m.port));
}

void rtpmidi_server_actor_t::handle_remote_gone(server_remote_gone_t &&m) {
  auto it = remotes_.find(m.remote);
  if (it == remotes_.end()) {
    return;
  }
  remotes_.erase(it);
  // The mdns actor removes the waiting ALSA port (it created it). A live
  // outbound session to a gone remote is closed.
  auto sit = sessions_.find(m.remote);
  if (sit != sessions_.end() && !sit->second.inbound) {
    stop_outbound_session(m.remote);
  }
}

void rtpmidi_server_actor_t::handle_session_request(session_request_t &&m) {
  if (m.subscribed) {
    auto sit = sessions_.find(m.remote);
    if (sit != sessions_.end()) {
      // Reuse (design D3): an existing session (usually inbound) serves
      // this port; no duplicate client is spawned.
      INFO("rtpmidi server {}: reusing existing {}bound session with '{}' "
           "for port {}.",
           name(), sit->second.inbound ? "in" : "out", m.remote, m.seq_port);
      if (m.port_id != 0 && router_mailbox_ && sit->second.peer_id != 0) {
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), m.port_id, sit->second.peer_id});
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), sit->second.peer_id, m.port_id});
      }
      if (sit->second.seq_port == 0) {
        sit->second.seq_port = m.seq_port;
      }
      if (alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_port_session_t{m.seq_port, true});
      }
      return;
    }
    if (remotes_.count(m.remote) == 0) {
      WARNING("rtpmidi server {}: session request for unknown remote '{}'.",
              name(), m.remote);
      return;
    }
    start_outbound_session(m.remote, m.seq_port, m.port_id);
  } else {
    auto sit = sessions_.find(m.remote);
    if (sit == sessions_.end()) {
      return; // nothing to tear down
    }
    if (sit->second.inbound) {
      // Shared with an inbound connection: the session stays; only the
      // port registration is handled by the listener.
      INFO("rtpmidi server {}: last unsubscribe on '{}'; session is inbound "
           "and stays.",
           name(), m.remote);
      return;
    }
    INFO("rtpmidi server {}: last unsubscribe on '{}'; closing the outbound "
         "session.",
         name(), m.remote);
    stop_outbound_session(m.remote);
  }
}

void rtpmidi_server_actor_t::start_outbound_session(const std::string &remote,
                                                    uint8_t seq_port,
                                                    peer_id_t port_id) {
  auto rit = remotes_.find(remote);
  if (rit == remotes_.end() || !router_mailbox_ || !worker_) {
    return;
  }
  INFO("rtpmidi server {}: starting outbound session to '{}' ({}:{}).",
       name(), remote, rit->second.hostname, rit->second.port);
  const auto corr = ++corr_counter_;
  pending_outbound_[corr] = outbound_pending_t{remote, seq_port, port_id};
  spawn_peer_t sp;
  sp.hdr = hdr_t{corr};
  sp.reply_to = mailbox();
  sp.type = "network_rtpmidi_peer_t";
  sp.meta = remote;
  const auto hostname = rit->second.hostname;
  const auto port = rit->second.port;
  const auto local_udp_port = rit->second.local_udp_port;
  auto worker = worker_;
  sp.factory = [worker, remote, hostname, port,
                local_udp_port](const mailbox_handle_t &sup, peer_id_t pid) {
    return std::make_shared<network_rtpmidi_peer_actor_t>(
        actor_config_t{.name = remote,
                       .scheduling = scheduling_class_t::elevated,
                       .rt_enabled = settings.rt_enable,
                       .rt_priority = settings.rt_priority,
                       .id = pid,
                       .supervisor_mailbox = sup},
        hostname, port, local_udp_port.empty() ? "0" : local_udp_port,
        worker);
  };
  router_mailbox_->post_control(std::move(sp));
}

void rtpmidi_server_actor_t::stop_outbound_session(const std::string &remote) {
  auto sit = sessions_.find(remote);
  if (sit == sessions_.end()) {
    return;
  }
  auto s = std::move(sit->second);
  sessions_.erase(sit);
  end_session(std::move(s), true);
}

void rtpmidi_server_actor_t::end_session(session_entry_t &&s,
                                         bool notify_listener) {
  INFO("rtpmidi server {}: session with '{}' ended.", name(), s.remote);
  session_by_initiator_.erase(s.initiator_id);
  by_initiator_.erase(s.initiator_id);
  by_ssrc_.erase(s.ssrc);
  if (notify_listener && s.seq_port != 0 && alsa_mailbox_) {
    // The listener unregisters the port when it has no ALSA subscribers.
    alsa_mailbox_->post_control(alsa_port_session_t{s.seq_port, false});
  }
  // The network peer goes with the session (the caller zeroes peer_id
  // when the peer is already gone).
  if (s.peer_id != 0 && router_mailbox_) {
    router_mailbox_->post_control(
        remove_peer_t{hdr_t{0}, mailbox_handle(), s.peer_id});
  }
  // Device peers die with the session: rawmidi peers are removed through
  // the router (closing the deferred-open fd); seq subscription peers are
  // removed through the listener (hosted ids + seq port).
  if (s.device_peer_id != 0) {
    if (s.kind == export_kind_e::seq) {
      if (alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_remove_port_t{
            hdr_t{0}, mailbox_handle(), s.device_peer_id, 0});
      }
    } else if (router_mailbox_) {
      router_mailbox_->post_control(
          remove_peer_t{hdr_t{0}, mailbox_handle(), s.device_peer_id});
    }
  }
}

// --- client-mode rawmidi (design D5: eager) ------------------------------------

void rtpmidi_server_actor_t::handle_rawmidi_client(server_rawmidi_client_t &&m) {
  // Client mode keeps today's eager behavior: open the device and connect
  // out at startup, holding the session for the daemon's lifetime.
  const int fd = ::open(m.device.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    ERROR("rtpmidi server {}: cannot open rawmidi device '{}': {}.", name(),
          m.device, strerror(errno));
    if (m.reply_to) {
      m.reply_to.post_control(
          ack_t{m.hdr, false, "cannot open rawmidi device " + m.device});
    }
    return;
  }
  const auto corr = ++corr_counter_;
  pending_rawmidi_[corr] = rawmidi_client_pending_t{m.name, 0};

  spawn_peer_t sp;
  sp.hdr = hdr_t{corr};
  sp.reply_to = mailbox();
  sp.type = "local_rawmidi_peer_t";
  sp.meta = m.name;
  const auto device = m.device;
  const auto rmname = m.name;
  sp.factory = [fd, device, rmname](const mailbox_handle_t &sup,
                                    peer_id_t pid) {
    return std::make_shared<local_rawmidi_peer_actor_t>(
        actor_config_t{.name = rmname.empty() ? device : rmname,
                       .scheduling = scheduling_class_t::elevated,
                       .rt_enabled = settings.rt_enable,
                       .rt_priority = settings.rt_priority,
                       .id = pid,
                       .supervisor_mailbox = sup},
        device, rmname, fd);
  };
  // Stash the network target in the pending entry: hostname/port travel in
  // a second map keyed by name.
  rawmidi_client_targets_[m.name] =
      rawmidi_client_target_t{m.hostname, m.remote_udp_port, m.local_udp_port,
                              corr};
  router_mailbox_->post_control(std::move(sp));
}

// --- replies --------------------------------------------------------------------

void rtpmidi_server_actor_t::handle_peer_ids_result(peer_ids_result_t &&m) {
  // 1. Waiting-port creations.
  {
    auto it = pending_ports_.find(m.hdr.corr);
    if (it != pending_ports_.end()) {
      const auto remote = std::move(it->second);
      pending_ports_.erase(it);
      auto rit = remotes_.find(remote);
      if (rit != remotes_.end()) {
        if (m.ids.empty()) {
          // Waiting ports are answered through alsa_port_result_t; a
          // peer_ids_result here is unexpected. Ignore.
          return;
        }
      }
      return;
    }
  }
  // 2. Outbound client spawns.
  {
    auto it = pending_outbound_.find(m.hdr.corr);
    if (it != pending_outbound_.end()) {
      auto po = std::move(it->second);
      pending_outbound_.erase(it);
      if (m.ids.empty()) {
        ERROR("rtpmidi server {}: outbound spawn for '{}' failed.", name(),
              po.remote);
        if (po.seq_port != 0 && alsa_mailbox_) {
          alsa_mailbox_->post_control(alsa_port_session_t{po.seq_port, false});
        }
        return;
      }
      session_entry_t s;
      s.remote = po.remote;
      s.peer_id = m.ids[0];
      s.inbound = false;
      s.seq_port = po.seq_port;
      sessions_[po.remote] = std::move(s);
      if (po.seq_port != 0 && alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_port_session_t{po.seq_port, true});
      }
      if (po.port_id != 0 && router_mailbox_) {
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), po.port_id, m.ids[0]});
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), m.ids[0], po.port_id});
      }
      INFO("rtpmidi server {}: outbound session to '{}' started (peer id {}).",
           name(), po.remote, m.ids[0]);
      return;
    }
  }
  // 3. Client-mode rawmidi: rawmidi peer ack, then the network client spawn.
  {
    auto it = pending_rawmidi_.find(m.hdr.corr);
    if (it != pending_rawmidi_.end()) {
      auto pr = std::move(it->second);
      pending_rawmidi_.erase(it);
      if (m.ids.empty()) {
        ERROR("rtpmidi server {}: rawmidi peer spawn for '{}' failed.", name(),
              pr.name);
        return;
      }
      pr.rawmidi_id = m.ids[0];
      auto tit = rawmidi_client_targets_.find(pr.name);
      if (tit == rawmidi_client_targets_.end()) {
        return;
      }
      auto target = std::move(tit->second);
      rawmidi_client_targets_.erase(tit);
      const auto corr = ++corr_counter_;
      pending_rawmidi_client_[corr] = rawmidi_client_pending_t{pr.name,
                                                               pr.rawmidi_id};
      spawn_peer_t sp;
      sp.hdr = hdr_t{corr};
      sp.reply_to = mailbox();
      sp.type = "network_rtpmidi_peer_t";
      sp.meta = pr.name;
      auto worker = worker_;
      sp.factory = [worker, name = pr.name, hostname = target.hostname,
                    port = target.remote_udp_port,
                    local_udp_port = target.local_udp_port](
                       const mailbox_handle_t &sup, peer_id_t pid) {
        return std::make_shared<network_rtpmidi_peer_actor_t>(
            actor_config_t{.name = name,
                           .scheduling = scheduling_class_t::elevated,
                           .rt_enabled = settings.rt_enable,
                           .rt_priority = settings.rt_priority,
                           .id = pid,
                           .supervisor_mailbox = sup},
            hostname, port, local_udp_port.empty() ? "0" : local_udp_port,
            worker);
      };
      router_mailbox_->post_control(std::move(sp));
      return;
    }
  }
  {
    auto it = pending_rawmidi_client_.find(m.hdr.corr);
    if (it != pending_rawmidi_client_.end()) {
      auto pr = std::move(it->second);
      pending_rawmidi_client_.erase(it);
      if (m.ids.empty()) {
        ERROR("rtpmidi server {}: network client spawn for rawmidi '{}' "
              "failed.",
              name(), pr.name);
        return;
      }
      if (router_mailbox_) {
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), pr.rawmidi_id, m.ids[0]});
        router_mailbox_->post_control(
            connect_t{hdr_t{0}, mailbox(), m.ids[0], pr.rawmidi_id});
      }
      session_entry_t s;
      s.remote = pr.name;
      s.peer_id = m.ids[0];
      s.device_peer_id = pr.rawmidi_id;
      s.inbound = false;
      sessions_[pr.name] = std::move(s);
      INFO("rtpmidi server {}: rawmidi client '{}' wired (rawmidi id {} <-> "
           "net id {}).",
           name(), pr.name, pr.rawmidi_id, m.ids[0]);
      return;
    }
  }
  // 4. Accept-link spawns (acceptor + device peers) and ALSA port results
  //    that answer create/set-registered/subscribe requests.
  for (auto &[initiator_id, link] : links_) {
    if (link.spawn_corr == m.hdr.corr) {
      if (m.ids.empty()) {
        ERROR("rtpmidi server {}: acceptor spawn for '{}' failed.", name(),
              link.remote);
        by_initiator_.erase(link.initiator_id);
        by_ssrc_.erase(link.ssrc);
        if (link.device_fd >= 0) {
          ::close(link.device_fd);
        }
        links_.erase(initiator_id);
        return;
      }
      link.net_id = m.ids[0];
      if (link.kind == export_kind_e::rawmidi) {
        // Spawn the rawmidi peer owning the open fd.
        const int fd = link.device_fd;
        link.device_fd = -1;
        auto eit = exports_.find(link.export_name);
        const auto target = eit != exports_.end() ? eit->second.target : "";
        spawn_peer_t sp;
        sp.hdr = hdr_t{link.rawmidi_corr};
        sp.reply_to = mailbox();
        sp.type = "local_rawmidi_peer_t";
        sp.meta = link.export_name;
        sp.factory = [fd, target, name = link.remote](
                         const mailbox_handle_t &sup, peer_id_t pid) {
          return std::make_shared<local_rawmidi_peer_actor_t>(
              actor_config_t{.name = target.empty() ? name : target,
                             .scheduling = scheduling_class_t::elevated,
                             .rt_enabled = settings.rt_enable,
                             .rt_priority = settings.rt_priority,
                             .id = pid,
                             .supervisor_mailbox = sup},
              target, name, fd);
        };
        router_mailbox_->post_control(std::move(sp));
        return;
      }
      wire_accept_link(link);
      return;
    }
    if (link.kind == export_kind_e::rawmidi && link.rawmidi_corr == m.hdr.corr) {
      if (m.ids.empty()) {
        ERROR("rtpmidi server {}: rawmidi peer spawn for '{}' failed.", name(),
              link.remote);
        // The acceptor (if any) is dropped too.
        if (link.net_id != 0 && router_mailbox_) {
          router_mailbox_->post_control(
              remove_peer_t{hdr_t{0}, mailbox_handle(), link.net_id});
        }
        by_initiator_.erase(link.initiator_id);
        by_ssrc_.erase(link.ssrc);
        links_.erase(initiator_id);
        return;
      }
      link.alsa_id = m.ids[0];
      wire_accept_link(link);
      return;
    }
  }
}

void rtpmidi_server_actor_t::handle_ack(ack_t &&m) {
  if (m.ok) {
    return;
  }
  // Failed spawn (preparation error): report; the waiting port stays so a
  // later subscription retries.
  {
    auto it = pending_outbound_.find(m.hdr.corr);
    if (it != pending_outbound_.end()) {
      ERROR("rtpmidi server {}: outbound spawn for '{}' failed: {}.", name(),
            it->second.remote, m.error);
      const auto seq_port = it->second.seq_port;
      pending_outbound_.erase(it);
      if (seq_port != 0 && alsa_mailbox_) {
        alsa_mailbox_->post_control(alsa_port_session_t{seq_port, false});
      }
      return;
    }
  }
  WARNING("rtpmidi server {}: request failed: {}.", name(), m.error);
}

void rtpmidi_server_actor_t::handle_peer_gone(udp_peer_gone_t &&m) {
  // A connection ended: drop the routing entries. Session bookkeeping
  // happens on the router's topology events (peer stopped/died/removed).
  by_initiator_.erase(m.initiator_id);
  by_ssrc_.erase(m.ssrc);
}

// --- control dispatch -----------------------------------------------------------

void rtpmidi_server_actor_t::on_control(server_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, session_request_t>) {
          handle_session_request(std::move(m));
        } else if constexpr (std::is_same_v<T, server_remote_discovered_t>) {
          handle_remote_discovered(std::move(m));
        } else if constexpr (std::is_same_v<T, server_remote_port_t>) {
          auto rit = remotes_.find(m.remote);
          if (rit != remotes_.end()) {
            rit->second.seq_port = m.seq_port;
          }
        } else if constexpr (std::is_same_v<T, server_remote_gone_t>) {
          handle_remote_gone(std::move(m));
        } else if constexpr (std::is_same_v<T, server_connect_to_t>) {
          handle_connect_to(std::move(m));
        } else if constexpr (std::is_same_v<T, server_rawmidi_client_t>) {
          handle_rawmidi_client(std::move(m));
        } else if constexpr (std::is_same_v<T, export_add_t>) {
          const bool ok = add_export(m);
          if (m.reply_to) {
            m.reply_to.post_control(
                ack_t{m.hdr, ok, ok ? "" : "export add failed"});
          }
        } else if constexpr (std::is_same_v<T, export_remove_t>) {
          remove_export(m.name);
          if (m.reply_to) {
            m.reply_to.post_control(ack_t{m.hdr, true, {}});
          }
        } else if constexpr (std::is_same_v<T, exports_status_req_t>) {
          handle_exports_status(std::move(m));
        } else if constexpr (std::is_same_v<T, peer_ids_result_t>) {
          handle_peer_ids_result(std::move(m));
        } else if constexpr (std::is_same_v<T, alsa_port_result_t>) {
          // Reply to create/set-registered/subscribe requests for accept
          // links and waiting-port creations.
          for (auto pit = pending_ports_.begin(); pit != pending_ports_.end();
               ++pit) {
            auto rit = remotes_.find(pit->second);
            if (rit != remotes_.end() && rit->second.corr == m.hdr.corr) {
              rit->second.seq_port = m.seq_port;
              rit->second.corr = 0;
              pending_ports_.erase(pit);
              INFO("rtpmidi server {}: waiting port for '{}' ready (seq {}).",
                   name(), rit->first, m.seq_port);
              break;
            }
          }
          for (auto &[initiator_id, link] : links_) {
            if (link.alsa_corr != m.hdr.corr) {
              continue;
            }
            if (m.peer_id == 0 && m.seq_port == 0) {
              ERROR("rtpmidi server {}: ALSA side for '{}' failed.", name(),
                    link.remote);
              if (link.net_id != 0 && router_mailbox_) {
                router_mailbox_->post_control(
                    remove_peer_t{hdr_t{0}, mailbox_handle(), link.net_id});
              }
              if (link.device_fd >= 0) {
                ::close(link.device_fd);
              }
              by_initiator_.erase(link.initiator_id);
              by_ssrc_.erase(link.ssrc);
              links_.erase(initiator_id);
              return;
            }
            link.alsa_id = m.peer_id;
            if (m.seq_port != 0) {
              link.seq_port = m.seq_port;
            }
            wire_accept_link(link);
            return;
          }
        } else if constexpr (std::is_same_v<T, ack_t>) {
          handle_ack(std::move(m));
        } else if constexpr (std::is_same_v<T, peer_event_t>) {
          // Topology event: a peer we spawned ended (self-terminated, died
          // or removed) — end its session.
          if (m.kind != peer_event_kind_t::stopped &&
              m.kind != peer_event_kind_t::died &&
              m.kind != peer_event_kind_t::removed) {
            return;
          }
          for (auto sit = sessions_.begin(); sit != sessions_.end(); ++sit) {
            if (sit->second.peer_id == m.peer_id ||
                sit->second.device_peer_id == m.peer_id) {
              auto s = std::move(sit->second);
              sessions_.erase(sit);
              if (s.peer_id == m.peer_id) {
                // The network peer is already gone; end_session drops
                // the device peer.
                s.peer_id = 0;
              } else {
                // The device peer is gone: the network side goes with it.
                s.device_peer_id = 0;
                if (s.peer_id != 0 && router_mailbox_) {
                  router_mailbox_->post_control(remove_peer_t{
                      hdr_t{0}, mailbox_handle(), s.peer_id});
                }
                s.peer_id = 0; // removed above; end_session must not repeat
              }
              end_session(std::move(s), true);
              return;
            }
          }
        } else if constexpr (std::is_same_v<T, udp_peer_gone_t>) {
          handle_peer_gone(std::move(m));
        } else if constexpr (std::is_same_v<T, udp_datagram_t>) {
          route_datagram(m.port, std::move(m.data));
        }
      },
      msg);
}

// --- exports status ---------------------------------------------------------------

void rtpmidi_server_actor_t::handle_exports_status(exports_status_req_t &&m) {
  if (!m.reply_to) {
    return;
  }
  exports_status_resp_t resp;
  resp.hdr = m.hdr;
  resp.source = "rtpmidi_server";
  for (auto &[name, exp] : exports_) {
    export_status_entry_t e;
    e.name = exp.name;
    e.kind = to_string(exp.kind);
    e.target = exp.target;
    e.port = exp.control_port;
    bool connected = false;
    for (auto &[remote, s] : sessions_) {
      if (s.export_name == name) {
        connected = true;
        break;
      }
    }
    e.state = connected ? "connected" : "listening";
    resp.exports.push_back(std::move(e));
  }
  // Waiting remotes are reported by the ALSA listener (it owns the
  // waiting ports); the server reports its listen-socket inventory only.
  m.reply_to.post_control(std::move(resp));
}

} // namespace rtpmididns
