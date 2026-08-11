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
#include "rtpmidid/logger.hpp"

namespace rtpmididns {

mdns_actor_t::mdns_actor_t(actor_config_t config) : actor_t(std::move(config)) {}

void mdns_actor_t::on_start() {
  try {
    // The avahi fds/timers are registered in THIS actor's poller.
    mdns_ = std::make_unique<rtpmidid::mdns_rtpmidi_t>(poller());
    INFO("mdns actor {}: avahi connected.", name());
  } catch (const std::exception &e) {
    ERROR("mdns actor {}: avahi setup failed: {}", name(), e.what());
    mdns_ = nullptr;
  }
}

void mdns_actor_t::on_control(control_message_t &&msg) {
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
            m.reply_to->post_control(std::move(resp));
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
        }
      },
      msg.v);
}

} // namespace rtpmididns
