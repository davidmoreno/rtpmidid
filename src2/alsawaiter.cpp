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

#include "alsawaiter.hpp"
#include "aseq.hpp"
#include "json.hpp"
#include "rtpmidid/rtpclient.hpp"
#include "settings.hpp"
#include <cstddef>

namespace rtpmididns {
alsawaiter_t::alsawaiter_t(const std::string &name_,
                           const std::string &hostname_,
                           const std::string &port_,
                           std::shared_ptr<aseq_t> aseq_)
    : name(name_), aseq(aseq_) {

  add_endpoint(hostname_, port_);

  alsaport = aseq->create_port(name);
  subscribe_connection = aseq->subscribe_event[alsaport].connect(
      [this](aseq_t::port_t from, const std::string &name) {
        connection_count++;
        if (connection_count == 1)
          connect_to_remote_server();
      });
  unsubscribe_connection =
      aseq->unsubscribe_event[alsaport].connect([this](aseq_t::port_t from) {
        connection_count--;
        if (connection_count <= 0)
          disconnect_from_remote_server();
      });
}

alsawaiter_t::~alsawaiter_t() { aseq->remove_port(alsaport); }

void alsawaiter_t::add_endpoint(const std::string &hostname,
                                const std::string &port) {
  DEBUG("Added endpoint for alsawaiter: {}, hostname: {}, port: {}", name,
        hostname, port);
  endpoints.push_back(alsawaiter_t::endpoint_t{hostname, port});
}

void alsawaiter_t::connect_to_remote_server() {
  WARNING("TODO connect to all known endpoints. Now only try first");
  if (endpoints.size() == 0) {
    WARNING(
        "Unknown endpoints for this alsa waiter. Dont know where to connect.");
    connection_count = 0;
    aseq->disconnect_port(alsaport);
    return;
  }

  // External index, in the future if first conneciton fails, try next
  // and so on. If all fail then real fail.
  endpoint_connect_index = 0;
  auto &endpoint = endpoints[endpoint_connect_index];

  DEBUG("Connect to remote server at {}:{}", endpoint.hostname, endpoint.port);
  rtpclient = std::make_shared<rtpmidid::rtpclient_t>(settings.rtpmidid_name);

  rtpclient->connect_to(endpoint.hostname, endpoint.port);
  // TODO connect all signals
  disconnect_connection = rtpclient->peer.disconnect_event.connect(
      [this](rtpmidid::rtppeer_t::disconnect_reason_e reason) {
        auto &endpoint = endpoints[endpoint_connect_index];
        // There was a conn failure, disconnect ALSA ports
        ERROR("Disconnected from peer: {}:{} reason: {}", endpoint.hostname,
              endpoint.port, reason);
        connection_count = 0;
        aseq->disconnect_port(alsaport);
      });
}

void alsawaiter_t::disconnect_from_remote_server() {
  auto &endpoint = endpoints[endpoint_connect_index];
  DEBUG("Disconnect from remote server at {}:{}", endpoint.hostname,
        endpoint.port);
  rtpclient = nullptr; // for me, this is dead
}

void alsawaiter_t::send_midi(midipeer_id_t from, const mididata_t &) {}
json_t alsawaiter_t::status() {
  json_t jendpoints;
  for (auto &endpoint : endpoints) {
    jendpoints.push_back(
        json_t{{"hostname", endpoint.hostname}, {"port", endpoint.port}});
  }

  return json_t{
      //
      {"name", name},
      {"type", "alsa_waiter"},
      {"endpoints", jendpoints},
      {"connection_count", connection_count}
      //
  };
}

} // namespace rtpmididns
