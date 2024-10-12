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

#include "factory.hpp"
#include "local_alsa_listener.hpp"
#include "local_alsa_multi_listener.hpp"
#include "local_alsa_peer.hpp"
#include "local_rawmidi_peer.hpp"
#include "midipeer.hpp"
#include "network_rtpmidi_client.hpp"
#include "network_rtpmidi_listener.hpp"
#include "network_rtpmidi_multi_listener.hpp"
#include "network_rtpmidi_peer.hpp"
#include <memory>

namespace rtpmididns {

std::shared_ptr<midipeer_t>
make_local_alsa_multi_listener(const std::string &name,
                               std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<local_alsa_multi_listener_t>(name, aseq);
}

std::shared_ptr<midipeer_t>
make_local_alsa_listener(std::shared_ptr<midirouter_t> &router,
                         const std::string &name, const std::string &hostname,
                         const std::string &port, std::shared_ptr<aseq_t> aseq,
                         const std::string &udp_port) {
  std::shared_ptr<midipeer_t> added;
  router->for_each_peer<local_alsa_listener_t>(
      [&](local_alsa_listener_t *peer) {
        if (peer->remote_name == name) {
          peer->add_endpoint(hostname, port);
          added = peer->shared_from_this();
        }
      });

  if (added)
    return added;

  auto ret = std::make_shared<local_alsa_listener_t>(name, hostname, port, aseq,
                                                     udp_port);
  return ret;
}

std::shared_ptr<midipeer_t> make_local_alsa_peer(const std::string &name,
                                                 std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<local_alsa_peer_t>(name, aseq);
}

std::shared_ptr<midipeer_t>
make_network_rtpmidi_client(std::shared_ptr<rtpmidid::rtpclient_t> peer) {
  return std::make_shared<network_rtpmidi_client_t>(peer);
}
std::shared_ptr<midipeer_t>
make_network_rtpmidi_client(const std::string &name,
                            const std::string &hostname,
                            const std::string &port) {
  return std::make_shared<network_rtpmidi_client_t>(name, hostname, port);
}

std::shared_ptr<midipeer_t>
make_network_rtpmidi_multi_listener(const std::string &name,
                                    const std::string &port,
                                    std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<network_rtpmidi_multi_listener_t>(name, port, aseq);
}

std::shared_ptr<midipeer_t>
make_network_rtpmidi_peer(std::shared_ptr<rtpmidid::rtppeer_t> peer) {
  return std::make_shared<network_rtpmidi_peer_t>(peer);
}

std::shared_ptr<midipeer_t>
make_network_rtpmidi_listener(const std::string &name,
                              const std::string &udp_port) {
  return std::make_shared<network_rtpmidi_listener_t>(name, udp_port);
}

std::shared_ptr<midipeer_t> make_rawmidi_peer(const std::string &name,
                                              const std::string &device) {
  return std::make_shared<local_rawmidi_peer_t>(name, device);
}

} // namespace rtpmididns