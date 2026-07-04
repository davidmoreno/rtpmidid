/**
 * Unified peer creation from device identity strings.
 */
#include "peer_factory.hpp"

#include "connection_alsa_direct.hpp"
#include "device_query.hpp"
#include "peer_device_alsa_seq.hpp"
#include "peer_device_rawmidi.hpp"
#include "peer_device_rtpmidi_client.hpp"
#include "peer_device_rtpmidi_session.hpp"
#include "peer_export_alsa_network.hpp"
#include "peer_export_rtpmidi_server.hpp"
#include "peer_import_alsa_rtp.hpp"
#include "peer_import_rtpmidi.hpp"
#include "peer_kind.hpp"
#include "webui_midi_monitor_peer.hpp"

#include <rtpmidid/rtpclient.hpp>

namespace rtpmididns {

namespace {

std::optional<std::string> require_field(const device_identity_t &id,
                                         std::string_view key, std::string *err) {
  const auto v = id.find(key);
  if (!v || v->empty()) {
    if (err)
      *err = FMT::format("identity missing required field '{}'", key);
    return std::nullopt;
  }
  return *v;
}

std::optional<std::pair<int, int>>
resolve_alsa_client_port(const peer_factory_context_t &ctx,
                         const std::string &client_name,
                         const std::string &port_name, std::string *err) {
  if (!ctx.aseq) {
    if (err)
      *err = "ALSA sequencer not available";
    return std::nullopt;
  }
  for (const auto &row : ctx.aseq->enumerate_exported_ports()) {
    if (row.client_name == client_name && row.port_name == port_name)
      return std::make_pair(row.client, row.port);
  }
  if (err)
    *err = FMT::format("ALSA port not found: {}:{}", client_name, port_name);
  return std::nullopt;
}

} // namespace

std::optional<std::shared_ptr<midipeer_t>>
create_peer(peer_create_request_t req, const peer_factory_context_t &ctx,
            std::string *err) {
  if (device_identity_is_stored_query(req.identity)) {
    if (err)
      *err = "cannot create peer from query (partial identity)";
    return std::nullopt;
  }

  const auto kind = peer_kind_from_identity_prefix(req.identity.type_prefix);
  if (!kind) {
    if (err)
      *err = FMT::format("unknown identity type '{}'", req.identity.type_prefix);
    return std::nullopt;
  }

  switch (*kind) {
  case peer_kind_e::device_alsa_seq: {
    if (!ctx.aseq) {
      if (err)
        *err = "ALSA sequencer not available";
      return std::nullopt;
    }
    const auto name = display_name_from_identity(req.identity);
    int sub_client = -1;
    int sub_port = -1;
    if (const auto c = req.identity.find("client")) {
      const auto p = require_field(req.identity, "port", err);
      if (!p)
        return std::nullopt;
      const auto resolved = resolve_alsa_client_port(ctx, *c, *p, err);
      if (!resolved)
        return std::nullopt;
      sub_client = resolved->first;
      sub_port = resolved->second;
    }
    return std::make_shared<peer_device_alsa_seq_t>(name, ctx.aseq, sub_client,
                                                  sub_port);
  }
  case peer_kind_e::device_rawmidi: {
    const auto device = require_field(req.identity, "device", err);
    if (!device)
      return std::nullopt;
    const auto name = req.identity.find("name").value_or(*device);
    return std::make_shared<peer_device_rawmidi_t>(name, *device);
  }
  case peer_kind_e::device_rtpmidi_client: {
    if (req.attachment == peer_attachment_kind_e::rtpclient && req.rtpclient)
      return std::make_shared<peer_device_rtpmidi_client_t>(req.rtpclient);
    const auto hostname = require_field(req.identity, "hostname", err);
    if (!hostname)
      return std::nullopt;
    const auto service = require_field(req.identity, "service", err);
    if (!service)
      return std::nullopt;
    const std::string port = req.identity.find("port").value_or("5004");
    return std::make_shared<peer_device_rtpmidi_client_t>(*service, *hostname,
                                                         port);
  }
  case peer_kind_e::device_rtpmidi_session: {
    if (req.attachment != peer_attachment_kind_e::rtppeer || !req.rtppeer) {
      if (err)
        *err = "rtpmidi_session requires active RTP connection";
      return std::nullopt;
    }
    return std::make_shared<peer_device_rtpmidi_session_t>(req.rtppeer);
  }
  case peer_kind_e::export_rtpmidi_server: {
    const auto name = require_field(req.identity, "name", err);
    if (!name)
      return std::nullopt;
    const std::string port = req.identity.find("port").value_or("");
    return std::make_shared<peer_export_rtpmidi_server_t>(*name, port);
  }
  case peer_kind_e::import_rtpmidi: {
    if (!ctx.aseq) {
      if (err)
        *err = "ALSA sequencer not available";
      return std::nullopt;
    }
    const auto name = require_field(req.identity, "name", err);
    if (!name)
      return std::nullopt;
    const auto port = require_field(req.identity, "port", err);
    if (!port)
      return std::nullopt;
    return std::make_shared<peer_import_rtpmidi_t>(*name, *port, ctx.aseq);
  }
  case peer_kind_e::export_alsa_network: {
    if (!ctx.aseq) {
      if (err)
        *err = "ALSA sequencer not available";
      return std::nullopt;
    }
    const auto name = require_field(req.identity, "name", err);
    if (!name)
      return std::nullopt;
    return std::make_shared<peer_export_alsa_network_t>(*name, ctx.aseq);
  }
  case peer_kind_e::import_alsa_rtp: {
    if (!ctx.aseq || !ctx.router) {
      if (err)
        *err = "router and ALSA sequencer required for alsa_listener";
      return std::nullopt;
    }
    std::string remote_name =
        req.identity.find("service").value_or(
            req.identity.find("name").value_or(""));
    if (remote_name.empty()) {
      if (err)
        *err = "alsa_listener requires service or name field";
      return std::nullopt;
    }
    const std::string hostname =
        req.identity.find("hostname").value_or("");
    const std::string port = req.identity.find("port").value_or("5004");
    const std::string local_udp =
        req.identity.find("local_udp_port").value_or("0");

    std::shared_ptr<midipeer_t> added;
    ctx.router->for_each_peer<peer_import_alsa_rtp_t>(
        [&](peer_import_alsa_rtp_t *peer) {
          if (peer->remote_name == remote_name) {
            if (!hostname.empty())
              peer->add_endpoint(hostname, port);
            added = peer->shared_from_this();
          }
        });
    if (added)
      return added;

    if (hostname.empty()) {
      return std::make_shared<peer_import_alsa_rtp_t>(remote_name, "", port,
                                                     ctx.aseq, local_udp);
    }
    return std::make_shared<peer_import_alsa_rtp_t>(remote_name, hostname, port,
                                                    ctx.aseq, local_udp);
  }
  case peer_kind_e::webui_monitor: {
    if (!req.target_peer_id || !req.monitor_uuid) {
      if (err)
        *err = "webui_monitor requires target_peer_id and monitor_uuid";
      return std::nullopt;
    }
    return make_webui_midi_monitor_peer(*req.monitor_uuid, *req.target_peer_id);
  }
  case peer_kind_e::unknown:
    break;
  }

  if (err)
    *err = "unsupported peer identity type";
  return std::nullopt;
}

std::optional<std::shared_ptr<midipeer_t>>
create_peer_from_string(std::string_view identity, const peer_factory_context_t &ctx,
                        std::string *err) {
  const auto parsed = device_identity_t::parse(identity);
  if (!parsed) {
    if (err)
      *err = FMT::format("invalid device identity '{}'", identity);
    return std::nullopt;
  }
  peer_create_request_t req;
  req.identity = *parsed;
  return create_peer(std::move(req), ctx, err);
}

} // namespace rtpmididns
