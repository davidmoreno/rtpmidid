/**
 * Find-or-create peers and spawn multi-peer connection recipes.
 */
#include "peer_spawn.hpp"

#include <stdexcept>

#include "device_identity_from_peer.hpp"
#include "device_query.hpp"
#include "midirouter.hpp"
#include "peer_export_rtpmidi_server.hpp"
#include "peer_import_alsa_rtp.hpp"
#include "peer_import_rtpmidi.hpp"
#include "settings.hpp"

#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtppeer.hpp>

namespace rtpmididns {

peer_factory_context_t make_factory_context(
    const std::shared_ptr<aseq_t> &aseq,
    const std::shared_ptr<midirouter_t> &router,
    const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns) {
  return peer_factory_context_t{aseq, router, mdns};
}

std::vector<online_device_t>
collect_online_devices_from_router(const std::shared_ptr<midirouter_t> &router) {
  std::vector<online_device_t> out;
  if (!router)
    return out;
  for (const auto &row : router->status_rows()) {
    if (!row.id)
      continue;
    const auto identity = compute_device_identity(row);
    if (!identity)
      continue;
    online_device_t device;
    device.peer_id = static_cast<peer_id_t>(*row.id);
    device.identity = *identity;
    out.push_back(std::move(device));
  }
  return out;
}

peer_id_t ensure_peer_for_identity(const peer_factory_context_t &ctx,
                                   std::shared_ptr<midirouter_t> router,
                                   std::string_view side) {
  const std::string side_str(side);
  const auto online = collect_online_devices_from_router(router);
  const auto indices = match_side_to_devices(side_str, online);
  if (!indices.empty())
    return online[indices.front()].peer_id;

  if (device_identity_is_stored_query(side_str))
    throw std::runtime_error(
        FMT::format("cannot create peer from query '{}'", side_str));

  std::string err;
  auto peer = create_peer_from_string(side_str, ctx, &err);
  if (!peer)
    throw std::runtime_error(err.empty() ? "failed to create peer" : err);
  const auto pid = router->add_peer(*peer);
  if (pid == 0)
    throw std::runtime_error("router.add_peer failed for '" + side_str + "'");
  return pid;
}

void spawn_import_rtpmidi_connection(const peer_factory_context_t &ctx,
                                     std::shared_ptr<midirouter_t> router,
                                     std::shared_ptr<rtpmidid::rtppeer_t> peer) {
  const auto session_identity = identity_from_rtppeer(*peer);
  if (!session_identity)
    return;

  device_identity_t alsa_id;
  alsa_id.type_prefix = "alsa_seq";
  alsa_id.fields.push_back(
      device_identity_field_t{"name", peer->remote_name, false});

  peer_create_request_t session_req;
  session_req.identity = *session_identity;
  session_req.attachment = peer_attachment_kind_e::rtppeer;
  session_req.rtppeer = peer;

  std::string err;
  auto session_peer = create_peer(session_req, ctx, &err);
  if (!session_peer)
    return;

  auto alsa_peer = create_peer({alsa_id}, ctx, &err);
  if (!alsa_peer)
    return;

  const auto alsa_id_num = router->add_peer(*alsa_peer);
  const auto session_id = router->add_peer(*session_peer);
  router->connect(alsa_id_num, session_id);
  router->connect(session_id, alsa_id_num);
}

peer_id_t spawn_alsa_network_server(const peer_factory_context_t &ctx,
                                    std::shared_ptr<midirouter_t> router,
                                    peer_id_t parent_peer_id,
                                    const std::string &remote_alsa_name) {
  device_identity_t id;
  id.type_prefix = "rtpmidi_server";
  id.fields.push_back(device_identity_field_t{"name", remote_alsa_name, false});

  const auto online = collect_online_devices_from_router(router);
  for (const auto &dev : online) {
    if (dev.identity.type_prefix == "rtpmidi_server") {
      if (dev.identity.find("name") &&
          *dev.identity.find("name") == remote_alsa_name)
        return dev.peer_id;
    }
  }

  std::string err;
  auto peer = create_peer({id}, ctx, &err);
  if (!peer)
    throw std::runtime_error(err.empty() ? "failed to spawn rtpmidi_server"
                                         : err);
  const peer_id_t network_id = router->add_peer(*peer);
  router->connect(network_id, parent_peer_id);
  return network_id;
}

peer_id_t spawn_alsa_listener_client(const peer_factory_context_t &ctx,
                                   std::shared_ptr<midirouter_t> router,
                                   peer_id_t listener_peer_id,
                                   const std::string &local_portname,
                                   const std::string &hostname,
                                   const std::string &port) {
  auto rtpclient = std::make_shared<rtpmidid::rtpclient_t>(local_portname);

  const auto client_id =
      identity_from_rtpclient_connect(hostname, port, local_portname);
  if (!client_id)
    throw std::runtime_error("failed to build rtpmidi_client identity");

  peer_create_request_t req;
  req.identity = *client_id;
  req.attachment = peer_attachment_kind_e::rtpclient;
  req.rtpclient = rtpclient;

  std::string err;
  auto peer = create_peer(req, ctx, &err);
  if (!peer)
    throw std::runtime_error(err.empty() ? "failed to spawn rtpmidi_client"
                                         : err);

  const peer_id_t client_peer_id = router->add_peer(*peer);
  router->connect(client_peer_id, listener_peer_id);
  router->connect(listener_peer_id, client_peer_id);

  rtpclient->local_base_port_str = "0";
  rtpclient->add_server_addresses(
      {rtpmidid::rtpclient_t::endpoint_t{hostname, port}});

  return client_peer_id;
}

void apply_ini_connects(const peer_factory_context_t &ctx,
                        std::shared_ptr<midirouter_t> router,
                        const std::vector<settings_t::ini_connect_t> &connects) {
  for (const auto &c : connects) {
    const auto pa = ensure_peer_for_identity(ctx, router, c.from);
    const auto pb = ensure_peer_for_identity(ctx, router, c.to);
    const std::string dir = c.direction.value_or("both");
    if (dir == "a2b" || dir == "both")
      router->connect(pa, pb);
    if (dir == "b2a" || dir == "both")
      router->connect(pb, pa);
  }
}

} // namespace rtpmididns
