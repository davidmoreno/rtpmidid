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

#include "rtpmidiremotehandler.hpp"
#include "factory.hpp"
#include "local_alsa_listener.hpp"
#include "midirouter.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"

namespace rtpmididns {
extern std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

rtpmidi_remote_handler_t::rtpmidi_remote_handler_t(
    std::shared_ptr<midirouter_t> router_, std::shared_ptr<aseq_t> aseq_)
    : router(router_), aseq(aseq_) {

  discover_connection = rtpmididns::mdns->discover_event.connect(
      [this](const std::string &name, const std::string &hostname,
             const std::string &port) { discover_peer(name, hostname, port); });

  remove_connection = rtpmididns::mdns->remove_event.connect(
      [this](const std::string &name, const std::string &hostname,
             const std::string &port) { remove_peer(name, hostname, port); });
}

void rtpmidi_remote_handler_t::discover_peer(const std::string &name,
                                             const std::string &hostname,
                                             const std::string &port) {
  DEBUG("Discover peer: {} {} {}", name, hostname, port);
  for (auto &peer : peers) {
    if (peer.name == name) {
      local_alsa_listener_t *alsawaiter =
          dynamic_cast<local_alsa_listener_t *>(peer.alsawaiter.get());
      if (alsawaiter)
        alsawaiter->add_endpoint(hostname, port);
      return;
    }
  }

  auto peer =
      rtpmididns::make_local_alsa_listener(router, name, hostname, port, aseq);
  peers.push_back(known_remote_peer_t{name, peer});

  router->add_peer(peer);
}

void rtpmidi_remote_handler_t::remove_peer(const std::string &name,
                                           const std::string &hostname,
                                           const std::string &port) {
  DEBUG("Remove peer: {} {} {}", name, hostname, port);
  std::vector<known_remote_peer_t>::iterator I = peers.begin(),
                                             endI = peers.end();
  for (; I != endI; ++I) {
    if (I->name == name) {
      INFO("Remove remote peer {} / midipeer {}", I->name,
           I->alsawaiter->peer_id);
      router->remove_peer(I->alsawaiter->peer_id);
      peers.erase(I);
    }
  }
}
} // namespace rtpmididns
