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

#include "local_alsa_listener.hpp"
#include "aseq.hpp"
#include "factory.hpp"
#include "json.hpp"
#include "local_alsa_peer.hpp"
#include "mididata.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/rtpclient.hpp"

namespace rtpmididns {
local_alsa_listener_t::local_alsa_listener_t(const std::string &name_,
                                             const std::string &hostname_,
                                             const std::string &port_,
                                             std::shared_ptr<aseq_t> aseq_,
                                             const std::string &local_udp_port)
    : local_udp_port(local_udp_port), remote_name(name_), aseq(aseq_) {

  add_endpoint(hostname_, port_);

  alsaport = aseq->create_port(remote_name);
  subscribe_connection = aseq->subscribe_event[alsaport].connect(
      [this](aseq_t::port_t from, const std::string &name) {
        connection_count++;
        DEBUG("ALSA subscribed event from {} to {}. count {}", from, name,
              connection_count);
        if (connection_count == 1)
          connect_to_remote_server(name);
      });
  unsubscribe_connection =
      aseq->unsubscribe_event[alsaport].connect([this](aseq_t::port_t from) {
        // The connection count is giving me problems as the connection is
        // sending two events, but disconnect only one. I could check  for
        // duplicates but I decided to count again here
        //
        // connection_count--;
        //

        connection_count = 0;
        auto myport = aseq_t::port_t{aseq->client_id, alsaport};
        aseq->for_connections(myport, [&](const aseq_t::port_t &port) {
          DEBUG("Still connected from {} <> {}", myport, port);
          connection_count++;
        });

        DEBUG("ALSA unsubscribed from {} to {}, connection count: {}", from,
              this->remote_name, connection_count);
        if (connection_count <= 0)
          disconnect_from_remote_server();
      });
  alsamidi_connection =
      aseq->midi_event[alsaport].connect([this](snd_seq_event_t *ev) {
        rtpmidid::io_bytes_static<1024> data;
        auto datawriter = rtpmidid::io_bytes_writer(data);
        mididata_decoder.ev_to_mididata_f(
            ev, datawriter, [this](const mididata_t &mididata) {
              router->send_midi(peer_id, mididata);
            });
      });
}

local_alsa_listener_t::~local_alsa_listener_t() {
  aseq->remove_port(alsaport);
  INFO("Remove ALSA port: {}, peer_id: {}. I remove also all connected "
       "local_alsa_peers_t",
       alsaport, peer_id);
  router->for_each_peer<local_alsa_peer_t>(
      [&](local_alsa_peer_t *peer) { router->remove_peer(peer->peer_id); });
}

void local_alsa_listener_t::add_endpoint(const std::string &hostname,
                                         const std::string &port) {
  DEBUG("Added endpoint for alsawaiter: {}, hostname: {}, port: {}",
        remote_name, hostname, port);
  bool exists = false;

  for (auto &endpoint : endpoints) {
    if (endpoint.hostname == hostname && endpoint.port == port) {
      exists = true;
      WARNING("Endpoint {}:{} already exists. May happen if several network "
              "interfaces. Ignoring.",
              hostname, port);
      break;
    }
  }

  if (!exists)
    endpoints.push_back(rtpmidid::rtpclient_t::endpoint_t{hostname, port});
}

void local_alsa_listener_t::connect_to_remote_server(
    const std::string &portname) {
  if (endpoints.size() == 0) {
    WARNING("Unknown endpoints for this alsa waiter. Dont know where to "
            "connect.");
    connection_count = 0;
    aseq->disconnect_port(alsaport);
    return;
  }

  // External index, in the future if first connection fails, try next
  // and so on. If all fail then real fail.
  local_name = portname;
  auto rtpclient = std::make_shared<rtpmidid::rtpclient_t>(portname);

  rtpmidiclientworker_peer_id =
      router->add_peer(make_network_rtpmidi_client(rtpclient));
  router->connect(rtpmidiclientworker_peer_id, peer_id);
  router->connect(peer_id, rtpmidiclientworker_peer_id);

  rtpclient->local_base_port_str = local_udp_port;
  rtpclient->add_server_addresses(endpoints);
}

void local_alsa_listener_t::disconnect_from_remote_server() {
  DEBUG("Disconnect from remote server at {}:{}", hostname, port);
  router->remove_peer(rtpmidiclientworker_peer_id);
  // rtpclient = nullptr; // for me, this is dead
  local_name = "";
}

void local_alsa_listener_t::send_midi(midipeer_id_t from,
                                      const mididata_t &data) {
  mididata_t mididata{data};
  mididata_encoder.mididata_to_evs_f(mididata, [this](snd_seq_event_t *ev) {
    snd_seq_ev_set_source(ev, alsaport);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    auto result = snd_seq_event_output(aseq->seq, ev);
    if (result < 0) {
      ERROR("Error: {}", snd_strerror(result));
      snd_seq_drop_input(aseq->seq);
      snd_seq_drop_output(aseq->seq);
    }
    result = snd_seq_drain_output(aseq->seq);
    if (result < 0) {
      ERROR("Error: {}", snd_strerror(result));
      snd_seq_drop_input(aseq->seq);
      snd_seq_drop_output(aseq->seq);
    }
  });
}

json_t local_alsa_listener_t::status() {
  json_t jendpoints;
  for (auto &endpoint : endpoints) {
    jendpoints.push_back(
        json_t{{"hostname", endpoint.hostname}, {"port", endpoint.port}});
  }
  std::string status;
  if (connection_count > 0)
    status = "CONNECTED";
  else
    status = "WAITING";

  return json_t{
      //
      {"name",
       std::format("{} <-> {}", local_name == "" ? "[WATING]" : local_name,
                   remote_name)},
      {"endpoints", jendpoints},
      {"connection_count", connection_count},
      {"status", status}
      //
  };
}

json_t local_alsa_listener_t::command(const std::string &cmd,
                                      const json_t &data) {
  if (cmd == "add_endpoint") {
    std::string hostname = data["hostname"];
    std::string port;
    if (data["port"].is_number()) {
      port = std::to_string(data["port"].get<int>());
    } else {
      port = data["port"];
    }
    add_endpoint(hostname, port);
    return json_t{"ok"};
  }
  if (cmd == "remove_endpoint") {
    std::string hostname = data["hostname"];
    std::string port;
    if (data["port"].is_number()) {
      port = std::to_string(data["port"].get<int>());
    } else {
      port = data["port"];
    }
    for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
      if (it->hostname == hostname && it->port == port) {
        DEBUG("Removing endpoint {}:{} from {}", hostname, port, remote_name);
        endpoints.erase(it);
        return json_t{"ok"};
      }
      ERROR("Try to remove endpoint {}:{} but not found", hostname, port);
    }
    return json_t{"error", "Endpoint not found"};
  }
  if (cmd == "help") {
    return json_t{{
        {{"name", "add_endpoint"},
         {"description", "Add an endpoint to connect to"}},
        {{"name", "remove_endpoint"},
         {"description", "Remove an endpoint to connect to"}},
    }};
  }

  return midipeer_t::command(cmd, data);
}
} // namespace rtpmididns
