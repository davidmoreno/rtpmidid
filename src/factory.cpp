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
#include "peer_import_alsa_rtp.hpp"
#include "peer_export_alsa_network.hpp"
#include "peer_device_alsa_seq.hpp"
#include "peer_device_rawmidi.hpp"
#include "midipeer.hpp"
#include "peer_device_rtpmidi_client.hpp"
#include "peer_export_rtpmidi_server.hpp"
#include "peer_import_rtpmidi.hpp"
#include "peer_device_rtpmidi_session.hpp"
#include <memory>

namespace rtpmididns {

std::shared_ptr<midipeer_t>
make_peer_export_alsa_network(const std::string &name,
                               std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<peer_export_alsa_network_t>(name, aseq);
}

std::shared_ptr<midipeer_t>
make_peer_import_alsa_rtp(std::shared_ptr<midirouter_t> &router,
                         const std::string &name, const std::string &hostname,
                         const std::string &port, std::shared_ptr<aseq_t> aseq,
                         const std::string &udp_port) {
  std::shared_ptr<midipeer_t> added;
  router->for_each_peer<peer_import_alsa_rtp_t>(
      [&](peer_import_alsa_rtp_t *peer) {
        if (peer->remote_name == name) {
          peer->add_endpoint(hostname, port);
          added = peer->shared_from_this();
        }
      });

  if (added)
    return added;

  auto ret = std::make_shared<peer_import_alsa_rtp_t>(name, hostname, port, aseq,
                                                     udp_port);
  return ret;
}

std::shared_ptr<midipeer_t> make_peer_device_alsa_seq(const std::string &name,
                                                 std::shared_ptr<aseq_t> aseq) {
  return make_peer_device_alsa_seq(name, aseq, -1, -1);
}

std::shared_ptr<midipeer_t>
make_peer_device_alsa_seq(const std::string &name, std::shared_ptr<aseq_t> aseq,
                     int subscribe_from_client, int subscribe_from_port) {
  return std::make_shared<peer_device_alsa_seq_t>(name, aseq, subscribe_from_client,
                                             subscribe_from_port);
}

std::shared_ptr<midipeer_t>
make_peer_device_rtpmidi_client(std::shared_ptr<rtpmidid::rtpclient_t> peer) {
  return std::make_shared<peer_device_rtpmidi_client_t>(peer);
}
std::shared_ptr<midipeer_t>
make_peer_device_rtpmidi_client(const std::string &name,
                            const std::string &hostname,
                            const std::string &port) {
  return std::make_shared<peer_device_rtpmidi_client_t>(name, hostname, port);
}

std::shared_ptr<midipeer_t>
make_peer_import_rtpmidi(const std::string &name,
                                    const std::string &port,
                                    std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<peer_import_rtpmidi_t>(name, port, aseq);
}

std::shared_ptr<midipeer_t>
make_peer_device_rtpmidi_session(std::shared_ptr<rtpmidid::rtppeer_t> peer) {
  return std::make_shared<peer_device_rtpmidi_session_t>(peer);
}

std::shared_ptr<midipeer_t>
make_peer_export_rtpmidi_server(const std::string &name,
                              const std::string &udp_port) {
  return std::make_shared<peer_export_rtpmidi_server_t>(name, udp_port);
}

std::shared_ptr<midipeer_t> make_peer_device_rawmidi(const std::string &name,
                                              const std::string &device) {
  return std::make_shared<peer_device_rawmidi_t>(name, device);
}

void create_rawmidi_rtpclient_pair(
    rtpmididns::midirouter_t *router,
    const ::rtpmididns::settings_t::rawmidi_t &rawmidi) {
  std::string name = rawmidi.name;
  auto rawmidi_peer =
      rtpmididns::make_peer_device_rawmidi(rawmidi.name, rawmidi.device);
  if (name == "") {
    name = dynamic_cast<rtpmididns::peer_device_rawmidi_t *>(rawmidi_peer.get())
               ->name;
  }
  router->add_peer(rawmidi_peer);
  std::shared_ptr<rtpmididns::midipeer_t> rtppeer;

  if (rawmidi.hostname.empty()) {
    INFO("Creating rawmidi peer={} as listener at udp_port={}", name,
         rawmidi.local_udp_port);
    rtppeer =
        rtpmididns::make_peer_export_rtpmidi_server(name, rawmidi.local_udp_port);
  } else {
    INFO("Creating rawmidi peer={} as client to hostname={} udp_port={}", name,
         rawmidi.hostname, rawmidi.remote_udp_port);
    rtppeer = rtpmididns::make_peer_device_rtpmidi_client(name, rawmidi.hostname,
                                                      rawmidi.remote_udp_port);
  }
  router->add_peer(rtppeer);

  router->connect(rawmidi_peer->peer_id, rtppeer->peer_id);
  router->connect(rtppeer->peer_id, rawmidi_peer->peer_id);
}

} // namespace rtpmididns