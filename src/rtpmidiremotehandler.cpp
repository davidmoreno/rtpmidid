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
#include "settings.hpp"

// Do nothing or redefine DEBUG0 to DEBUG
#define DEBUG0(...)

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
  if (!check_if_add_peer(name, hostname, port)) {
    INFO("Not adding peer name={} hostname={} port={}, as requested by "
         "settings",
         name, hostname, port);
    return;
  }
  INFO("Discover peer: name={} address={} port={}", name, hostname, port);

  for (auto &peer : peers) {
    if (peer.name == name) {
      local_alsa_listener_t *alsawaiter =
          dynamic_cast<local_alsa_listener_t *>(peer.alsawaiter.get());
      if (alsawaiter)
        alsawaiter->add_endpoint(hostname, port);
      DEBUG("Reuse peer: name={} address={}:{}", name, hostname, port);
      return;
    }
  }

  DEBUG("New peer: name={} address={}:{}", name, hostname, port);
  auto peer = rtpmididns::make_local_alsa_listener(router, name, hostname, port,
                                                   aseq, "0");
  peers.push_back(known_remote_peer_t{name, peer});

  router->add_peer(peer);
}

void rtpmidi_remote_handler_t::remove_peer(const std::string &name,
                                           const std::string &hostname,
                                           const std::string &port) {
  DEBUG("Remove peer: name=\"{}\" address={}:{}", name, hostname, port);
  std::vector<known_remote_peer_t>::iterator I = peers.begin(),
                                             endI = peers.end();
  for (; I != endI; ++I) {
    if (I->name == name) {
      INFO("Remove remote peer {} / midipeer {}", I->name,
           I->alsawaiter->peer_id);
      router->remove_peer(I->alsawaiter->peer_id);
      peers.erase(I);
      return;
    }
  }
}

bool rtpmidi_remote_handler_t::check_if_add_peer(const std::string &name,
                                                 const std::string &hostname,
                                                 const std::string &port) {
  if (settings.rtpmidi_discover.enabled == false) {
    return false;
  }

  std::string fullname = fmt::format("{}:{} - {}", hostname, port, name);
  DEBUG0("Checking if we should add peer: fullname=\"{}\"", fullname);
  bool match_negative = std::regex_search(
      fullname, settings.rtpmidi_discover.name_negative_regex);
  DEBUG0("Match negative: {}", match_negative);
  if (match_negative) {
    return false;
  }

  bool match_positive = std::regex_search(
      fullname, settings.rtpmidi_discover.name_positive_regex);
  DEBUG0("Match positive: {}", match_positive);
  if (match_positive) {
    return true;
  }
  return false;
}

} // namespace rtpmididns
