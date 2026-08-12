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

/// Dedicated mdns actor (design D13; task 6.3): owns the avahi fds in its
/// own poller at normal scheduling priority, so mdns neither contends with
/// the data plane nor stalls control. Serves status and announcement
/// mutations via mailbox request/response to control connection actors.

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "worker_actor.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace rtpmididns {

class mdns_actor_t : public actor_t<std::monostate, mdns_control_t> {
public:
  using control_messages = mdns_control_t;
public:
  explicit mdns_actor_t(actor_config_t config,
                        std::shared_ptr<router_mailbox_t> router_mailbox = {},
                        std::shared_ptr<alsa_mailbox_t> alsa_mailbox = {},
                        std::shared_ptr<worker_actor_t> worker = {});

  /// The mdns object (for tests that need the legacy global-free access).
  rtpmidid::mdns_rtpmidi_t *mdns() const { return mdns_.get(); }
  /// Handle a discovery event (called by the avahi callback; also directly
  /// by tests).
  void on_discovered(const std::string &name, const std::string &address,
                     const std::string &port);
  void on_removed(const std::string &name);

protected:
  void on_start() override;
  void on_stop() override;
  void on_control(mdns_control_t &&msg) override;

private:
  bool accept_discovery(const std::string &name, const std::string &address,
                        const std::string &port) const;
  void cleanup_entry(const std::string &name);

  std::unique_ptr<rtpmidid::mdns_rtpmidi_t> mdns_;
  std::shared_ptr<router_mailbox_t> router_mailbox_;
  std::shared_ptr<alsa_mailbox_t> alsa_mailbox_;
  std::shared_ptr<worker_actor_t> worker_;
  rtpmidid::signal_t<const std::string &, const std::string &,
                     const std::string &>::connection_t
      discover_conn_;
  rtpmidid::signal_t<const std::string &, const std::string &,
                     const std::string &>::connection_t
      remove_conn_;

  struct discovery_entry_t {
    std::string name;
    std::string address;
    std::string port;
    uint64_t corr = 0;
    peer_id_t alsa_id = 0;
    peer_id_t net_id = 0;
  };
  std::unordered_map<std::string, discovery_entry_t> discovered_;
  uint64_t corr_counter_ = 0;
};

} // namespace rtpmididns
