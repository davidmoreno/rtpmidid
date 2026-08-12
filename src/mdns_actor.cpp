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

#include "mdns_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"

namespace rtpmididns {

mdns_actor_t::mdns_actor_t(actor_config_t config,
                           std::shared_ptr<router_mailbox_t> router_mailbox,
                           std::shared_ptr<alsa_mailbox_t> alsa_mailbox,
                           std::shared_ptr<worker_actor_t> worker)
    : actor_t(std::move(config)), router_mailbox_(std::move(router_mailbox)),
      alsa_mailbox_(std::move(alsa_mailbox)), worker_(std::move(worker)) {}

void mdns_actor_t::on_stop() {
  // Tear the avahi client down here, while this actor's thread is still
  // running: frees the dbus connection and removes the avahi watches from
  // this actor's poller, so no watch callback can fire on freed state.
  discover_conn_.disconnect();
  remove_conn_.disconnect();
  mdns_.reset();
}

void mdns_actor_t::on_start() {
  try {
    // The avahi fds/timers are registered in THIS actor's poller.
    mdns_ = std::make_unique<rtpmidid::mdns_rtpmidi_t>(poller());
    INFO("mdns actor {}: avahi connected.", name());
  } catch (const std::exception &e) {
    ERROR("mdns actor {}: avahi setup failed: {}", name(), e.what());
    mdns_ = nullptr;
    return;
  }
  // Discovered rtpmidi servers become ALSA ports + network clients.
  discover_conn_ = mdns_->discover_event.connect(
      [this](const std::string &n, const std::string &a, const std::string &p) {
        on_discovered(n, a, p);
      });
  remove_conn_ = mdns_->remove_event.connect(
      [this](const std::string &n, const std::string &, const std::string &) {
        on_removed(n);
      });
}

bool mdns_actor_t::accept_discovery(const std::string &name,
                                    const std::string &address,
                                    const std::string &port) const {
  if (!settings.rtpmidi_discover.enabled) {
    return false;
  }
  const std::string fullname = FMT::format("{}:{} - {}", address, port, name);
  if (std::regex_search(fullname,
                        settings.rtpmidi_discover.name_negative_regex)) {
    return false;
  }
  return std::regex_search(fullname,
                           settings.rtpmidi_discover.name_positive_regex);
}

void mdns_actor_t::on_discovered(const std::string &name,
                                 const std::string &address,
                                 const std::string &port) {
  if (name.empty() || address.empty()) {
    return;
  }
  if (!accept_discovery(name, address, port)) {
    INFO("mdns: not adding discovered peer=\"{}\" (settings filter)", name);
    return;
  }
  if (discovered_.count(name) != 0) {
    return; // already known (dedupe by name)
  }
  if (!router_mailbox_ || !alsa_mailbox_ || !worker_) {
    WARNING("mdns: discovery wiring incomplete; ignoring {}", name);
    return;
  }
  INFO("mdns: discovered rtpmidi server \"{}\" at {}:{}; creating ALSA port + "
       "network client.",
       name, address, port);
  discovery_entry_t entry;
  entry.name = name;
  entry.address = address;
  entry.port = port;
  entry.corr = ++corr_counter_;
  discovered_[name] = std::move(entry);
  // 1. Ask the ALSA actor for a hosted port; the reply carries the router id.
  alsa_mailbox_->post_control(alsa_create_port_t{
      hdr_t{discovered_[name].corr}, mailbox_handle(), name,
      FMT::format("{}:{}", address, port)});
}

void mdns_actor_t::on_removed(const std::string &name) {
  auto it = discovered_.find(name);
  if (it == discovered_.end()) {
    return;
  }
  INFO("mdns: remote rtpmidi server \"{}\" gone; removing ALSA port + client.",
       name);
  auto entry = std::move(it->second);
  discovered_.erase(it);
  if (entry.net_id != 0 && router_mailbox_) {
    router_mailbox_->post_control(
        remove_peer_t{hdr_t{0}, mailbox_handle(), entry.net_id});
  }
  if (entry.alsa_id != 0 && alsa_mailbox_) {
    alsa_mailbox_->post_control(
        alsa_remove_port_t{hdr_t{0}, mailbox_handle(), entry.alsa_id});
  }
}

void mdns_actor_t::cleanup_entry(const std::string &name) {
  auto it = discovered_.find(name);
  if (it != discovered_.end()) {
    discovered_.erase(it);
  }
}

void mdns_actor_t::on_control(mdns_control_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, mdns_status_req_t>) {
          mdns_status_resp_t resp;
          resp.hdr = m.hdr;
          if (mdns_) {
            resp.available = true;
            for (auto &a : mdns_->announcements) {
              resp.announcements.push_back({a.name, a.port});
            }
            for (auto &r : mdns_->remote_announcements) {
              resp.remote_announcements.push_back({r.name, r.address, r.port});
            }
          }
          if (m.reply_to) {
            m.reply_to.post_control(std::move(resp));
          }
        } else if constexpr (std::is_same_v<T, mdns_announce_t>) {
          if (mdns_) {
            mdns_->announce_rtpmidi(m.name, m.port);
          }
        } else if constexpr (std::is_same_v<T, mdns_unannounce_t>) {
          if (mdns_) {
            mdns_->unannounce_rtpmidi(m.name, m.port);
          }
        } else if constexpr (std::is_same_v<T, mdns_remove_t>) {
          if (mdns_) {
            mdns_->remove_announcement(m.name, m.hostname, m.port);
          }
        } else if constexpr (std::is_same_v<T, peer_ids_result_t>) {
          // A create-port or spawn ack for a discovery in flight.
          for (auto &[key, entry] : discovered_) {
            if (entry.corr != m.hdr.corr) {
              continue;
            }
            if (m.ids.empty()) {
              ERROR("mdns: discovery \"{}\" failed (no ids).", key);
              cleanup_entry(key);
              return;
            }
            if (entry.alsa_id == 0) {
              // ALSA port created: spawn the network client.
              entry.alsa_id = m.ids[0];
              spawn_peer_t sp;
              sp.hdr = hdr_t{entry.corr};
              sp.reply_to = mailbox_handle();
              sp.type = "network_rtpmidi_peer_t";
              sp.meta = entry.name;
              sp.factory =
                  [worker = worker_, name = entry.name, address = entry.address,
                   port = entry.port](const mailbox_handle_t &sup,
                                      peer_id_t pid) {
                    return std::make_shared<network_rtpmidi_peer_actor_t>(
                        actor_config_t{.name = name,
                                       .id = pid,
                                       .supervisor_mailbox = sup},
                        address, port, "0", worker);
                  };
              router_mailbox_->post_control(std::move(sp));
            } else if (entry.net_id == 0) {
              // Network client spawned: connect both ways.
              entry.net_id = m.ids[0];
              router_mailbox_->post_control(connect_t{
                  hdr_t{0}, mailbox_handle(), entry.alsa_id, entry.net_id});
              router_mailbox_->post_control(connect_t{
                  hdr_t{0}, mailbox_handle(), entry.net_id, entry.alsa_id});
              INFO("mdns: \"{}\" wired (alsa id {} <-> network id {}).", key,
                   entry.alsa_id, entry.net_id);
            }
            return;
          }
        } else if constexpr (std::is_same_v<T, alsa_port_event_t>) {
          // "Network Export" bridge: wire/unwire the announced port to every
          // discovered remote's client.
          for (auto &[key, entry] : discovered_) {
            if (entry.net_id == 0) {
              continue;
            }
            if (m.subscribed) {
              router_mailbox_->post_control(connect_t{
                  hdr_t{0}, mailbox_handle(), m.port_id, entry.net_id});
              router_mailbox_->post_control(connect_t{
                  hdr_t{0}, mailbox_handle(), entry.net_id, m.port_id});
            } else {
              router_mailbox_->post_control(disconnect_t{
                  hdr_t{0}, mailbox_handle(), m.port_id, entry.net_id});
              router_mailbox_->post_control(disconnect_t{
                  hdr_t{0}, mailbox_handle(), entry.net_id, m.port_id});
            }
          }
        } else if constexpr (std::is_same_v<T, ack_t>) {
          // A failed spawn: drop the discovery entry (the alsa port stays;
          // the network side could not be created).
          for (auto &[key, entry] : discovered_) {
            if (entry.corr == m.hdr.corr && entry.net_id == 0 &&
                entry.alsa_id != 0) {
              ERROR("mdns: spawn failed for \"{}\": {}; removing alsa port.",
                    key, m.error);
              if (alsa_mailbox_) {
                alsa_mailbox_->post_control(alsa_remove_port_t{
                    hdr_t{0}, mailbox_handle(), entry.alsa_id});
              }
              cleanup_entry(key);
              return;
            }
          }
        }
      },
      msg);
}

} // namespace rtpmididns
