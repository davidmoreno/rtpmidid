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
#include "local_alsa_peer.hpp"
#include "mididata.hpp"
#include "peer_status_jsondm.hpp"
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
  if (aseq) {
    aseq->remove_port(alsaport);
  }
  INFO("Remove ALSA port: {}, peer_id: {}", alsaport, peer_id);
  if (router && rtpmidiclientworker_peer_id != MIDIPEER_ID_INVALID) {
    router->remove_peer(rtpmidiclientworker_peer_id);
    rtpmidiclientworker_peer_id = MIDIPEER_ID_INVALID;
  }
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

peer_status_variant_t local_alsa_listener_t::status() {
  alsa_listener_status_t s;
  for (auto &endpoint : endpoints) {
    s.endpoints.push_back(
        listener_endpoint_t{endpoint.hostname, endpoint.port});
  }
  if (connection_count > 0)
    s.status = "CONNECTED";
  else
    s.status = "WAITING";

  s.name = FMT::format("{} <-> {}", local_name == "" ? "[WATING]" : local_name,
                       remote_name);
  s.connection_count = connection_count;
  return s;
}

std::string local_alsa_listener_t::command(const std::string &cmd,
                                           std::string_view params_json) {
  if (cmd == "add_endpoint" || cmd == "remove_endpoint") {
    endpoint_params_t params;
    try {
      jsondm::deserialize(params_json, params);
    } catch (const jsondm::exception &) {
      std::string out;
      jsondm::serialize(peer_error_t{"Invalid endpoint params"}, out);
      return out;
    }
    std::string port = std::visit(
        [](const auto &p) -> std::string {
          if constexpr (std::is_same_v<std::decay_t<decltype(p)>, int>) {
            return std::to_string(p);
          } else {
            return p;
          }
        },
        params.port);
    if (cmd == "add_endpoint") {
      add_endpoint(params.hostname, port);
      std::string out;
      jsondm::serialize(std::vector<std::string>{"ok"}, out);
      return out;
    }
    for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
      if (it->hostname == params.hostname && it->port == port) {
        DEBUG("Removing endpoint {}:{} from {}", params.hostname, port,
              remote_name);
        endpoints.erase(it);
        std::string out;
        jsondm::serialize(std::vector<std::string>{"ok"}, out);
        return out;
      }
      ERROR("Try to remove endpoint {}:{} but not found", params.hostname,
            port);
    }
    std::string out;
    jsondm::serialize(peer_error_t{"Endpoint not found"}, out);
    return out;
  }
  if (cmd == "help") {
    std::string out;
    jsondm::serialize(
        std::vector<command_help_t>{
            {"add_endpoint", "Add an endpoint to connect to"},
            {"remove_endpoint", "Remove an endpoint to connect to"},
        },
        out);
    return out;
  }

  return midipeer_t::command(cmd, params_json);
}
} // namespace rtpmididns
