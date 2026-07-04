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

#include "peer_import_alsa_rtp.hpp"
#include "peer_stable_id.hpp"
#include "aseq.hpp"
#include "peer_spawn.hpp"
#include "peer_factory.hpp"
#include "device_identity_from_peer.hpp"
#include "dm_json_generated.hpp"
#include "peer_device_alsa_seq.hpp"
#include "mididata.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/rtpclient.hpp"

namespace rtpmididns {
peer_import_alsa_rtp_t::peer_import_alsa_rtp_t(const std::string &name_,
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
              enqueue_to_router(mididata);
            });
      });
}

peer_import_alsa_rtp_t::~peer_import_alsa_rtp_t() {
  if (aseq) {
    aseq->remove_port(alsaport);
  }
  INFO("component=alsa_listener Remove ALSA port: {}, peer_id: {}", alsaport, peer_id);
  if (router && rtpmidiclientworker_peer_id != MIDIPEER_ID_INVALID) {
    router->remove_peer(rtpmidiclientworker_peer_id);
    rtpmidiclientworker_peer_id = MIDIPEER_ID_INVALID;
  }
}

void peer_import_alsa_rtp_t::add_endpoint(const std::string &hostname,
                                         const std::string &port) {
  DEBUG("Added endpoint for alsawaiter: {}, hostname: {}, port: {}",
        remote_name, hostname, port);
  bool exists = false;

  for (auto &endpoint : endpoints) {
    if (endpoint.hostname == hostname && endpoint.port == port) {
      exists = true;
      WARNING("component=alsa_listener Endpoint {}:{} already exists. May happen if several network "
              "interfaces. Ignoring.",
              hostname, port);
      break;
    }
  }

  if (!exists)
    endpoints.push_back(rtpmidid::rtpclient_t::endpoint_t{hostname, port});
}

void peer_import_alsa_rtp_t::connect_to_remote_server(
    const std::string &portname) {
  if (endpoints.size() == 0) {
    WARNING("component=alsa_listener Unknown endpoints. Dont know where to "
            "connect.");
    connection_count = 0;
    aseq->disconnect_port(alsaport);
    return;
  }

  // External index, in the future if first connection fails, try next
  // and so on. If all fail then real fail.
  local_name = portname;
  const auto &ep = endpoints.front();
  peer_factory_context_t ctx;
  ctx.aseq = aseq;
  ctx.router = router;

  auto rtpclient = std::make_shared<rtpmidid::rtpclient_t>(portname);
  const auto client_identity =
      identity_from_rtpclient_connect(ep.hostname, ep.port, portname);
  if (!client_identity)
    return;

  peer_create_request_t req;
  req.identity = *client_identity;
  req.attachment = peer_attachment_kind_e::rtpclient;
  req.rtpclient = rtpclient;

  std::string err;
  auto peer = create_peer(req, ctx, &err);
  if (!peer) {
    ERROR("component=alsa_listener client create failed: {}", err);
    connection_count = 0;
    aseq->disconnect_port(alsaport);
    return;
  }

  rtpmidiclientworker_peer_id = router->add_peer(*peer);
  router->connect(rtpmidiclientworker_peer_id, peer_id);
  router->connect(peer_id, rtpmidiclientworker_peer_id);

  rtpclient->local_base_port_str = local_udp_port;
  rtpclient->add_server_addresses(endpoints);
}

void peer_import_alsa_rtp_t::disconnect_from_remote_server() {
  DEBUG("Disconnect from remote server at {}:{}", hostname, port);
  router->remove_peer(rtpmidiclientworker_peer_id);
  // rtpclient = nullptr; // for me, this is dead
  local_name = "";
}

void peer_import_alsa_rtp_t::send_midi(midipeer_id_t from,
                                      const mididata_t &data) {
  mididata_t mididata{data};
  std::scoped_lock lock(aseq->output_mutex);
  mididata_encoder.mididata_to_evs_f(mididata, [this](snd_seq_event_t *ev) {
    snd_seq_ev_set_source(ev, alsaport);
    snd_seq_ev_set_subs(ev); // to all subscribers
    snd_seq_ev_set_direct(ev);
    auto result = snd_seq_event_output(aseq->seq, ev);
    if (result < 0) {
      ERROR("Error: {}", snd_strerror(result));
      snd_seq_drop_input(aseq->seq);
      snd_seq_drop_output(aseq->seq);
      return;
    }
    result = snd_seq_drain_output(aseq->seq);
    if (result < 0) {
      ERROR("Error: {}", snd_strerror(result));
      snd_seq_drop_input(aseq->seq);
      snd_seq_drop_output(aseq->seq);
    }
  });
}

router_peer_row_t peer_import_alsa_rtp_t::status() const {
  router_peer_row_t row;
  std::vector<listener_endpoint_t> eps;
  for (const auto &endpoint : endpoints) {
    listener_endpoint_t e;
    e.hostname = endpoint.hostname;
    e.port = endpoint.port;
    eps.push_back(std::move(e));
  }
  row.endpoints = std::move(eps);
  row.connection_count = connection_count;
  row.status = connection_count > 0 ? "CONNECTED" : "WAITING";
  row.name = FMT::format("{} <-> {}", local_name.empty() ? "[WAITING]" : local_name,
                         remote_name);
  return row;
}

bool peer_import_alsa_rtp_t::control_peer_command(std::string_view cmd,
                                                 std::string_view params_json,
                                                 ::rtpmididns::dmjson::writer_t &out,
                                                 std::string &out_error) {
  if (cmd == "add_endpoint") {
    listener_add_endpoint_params_t p{};
    if (!dmjson::from_json(params_json, p)) {
      out_error = "bad params";
      return false;
    }
    add_endpoint(p.hostname, p.port);
    dmjson::write_ok_array(out);
    return true;
  }
  if (cmd == "remove_endpoint") {
    listener_remove_endpoint_params_t p{};
    if (!dmjson::from_json(params_json, p)) {
      out_error = "bad params";
      return false;
    }
    for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
      if (it->hostname == p.hostname && it->port == p.port) {
        DEBUG("Removing endpoint {}:{} from {}", p.hostname, p.port, remote_name);
        endpoints.erase(it);
        dmjson::write_ok_array(out);
        return true;
      }
    }
    ERROR("component=alsa_listener Try to remove endpoint {}:{} but not found", p.hostname, p.port);
    out_error = "Endpoint not found";
    return false;
  }
  if (cmd == "help") {
    out.begin_array();
    {
      out.array_item();
      out.begin_object();
      out.key("name");
      out.string_value("add_endpoint");
      out.key("description");
      out.string_value("Add an endpoint to connect to");
      out.end_object();
    }
    {
      out.array_item();
      out.begin_object();
      out.key("name");
      out.string_value("remove_endpoint");
      out.key("description");
      out.string_value("Remove an endpoint to connect to");
      out.end_object();
    }
    out.end_array();
    return true;
  }
  return midipeer_t::control_peer_command(cmd, params_json, out, out_error);
}

} // namespace rtpmididns
