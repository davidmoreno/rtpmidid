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

#include "mdns_actor.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"

namespace rtpmididns {

mdns_actor_t::mdns_actor_t(actor_config_t config,
                           std::shared_ptr<alsa_mailbox_t> alsa_mailbox,
                           std::shared_ptr<server_mailbox_t> server_mailbox)
    : actor_t(std::move(config)), alsa_mailbox_(std::move(alsa_mailbox)),
      server_mailbox_(std::move(server_mailbox)) {}

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
  // Discovered rtpmidi servers become waiting ALSA ports (lazy: no client
  // is spawned until an ALSA subscriber asks for the session).
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
  if (!alsa_mailbox_ || !server_mailbox_) {
    WARNING("mdns: discovery wiring incomplete; ignoring {}", name);
    return;
  }
  INFO("mdns: discovered rtpmidi server \"{}\" at {}:{}; creating waiting "
       "ALSA port (lazy).",
       name, address, port);
  discovery_entry_t entry;
  entry.name = name;
  entry.address = address;
  entry.port = port;
  entry.corr = ++corr_counter_;
  discovered_[name] = std::move(entry);
  // 1. The ALSA listener creates the waiting port (unregistered).
  alsa_mailbox_->post_control(
      alsa_create_port_t{hdr_t{discovered_[name].corr}, mailbox_handle(), name,
                         FMT::format("{}:{}", address, port), true, name});
  // 2. The rtpmidi server records the outbound target; the session starts
  //    only when an ALSA client subscribes to the waiting port.
  server_mailbox_->post_control(
      server_remote_discovered_t{name, address, port});
}

void mdns_actor_t::on_removed(const std::string &name) {
  auto it = discovered_.find(name);
  if (it == discovered_.end()) {
    return;
  }
  INFO("mdns: remote rtpmidi server \"{}\" gone; removing waiting port.",
       name);
  auto entry = std::move(it->second);
  discovered_.erase(it);
  if (alsa_mailbox_ && entry.seq_port != 0) {
    alsa_mailbox_->post_control(
        alsa_remove_port_t{hdr_t{0}, mailbox_handle(), 0, entry.seq_port});
  }
  if (server_mailbox_) {
    server_mailbox_->post_control(server_remote_gone_t{name});
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
        } else if constexpr (std::is_same_v<T, alsa_port_result_t>) {
          // Waiting-port creation ack for a discovery in flight.
          for (auto &[key, entry] : discovered_) {
            if (entry.corr != m.hdr.corr) {
              continue;
            }
            if (m.seq_port == 0) {
              ERROR("mdns: waiting port for \"{}\" could not be created.",
                    key);
              discovered_.erase(key);
              return;
            }
            entry.seq_port = m.seq_port;
            INFO("mdns: waiting port for \"{}\" created (seq {}).", key,
                 m.seq_port);
            // The server learns the seq port (inbound reuse of the waiting
            // port).
            if (server_mailbox_) {
              server_mailbox_->post_control(
                  server_remote_port_t{key, m.seq_port});
            }
            return;
          }
        }
      },
      msg);
}

} // namespace rtpmididns
