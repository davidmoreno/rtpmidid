/**
 * Find-or-create peers and spawn multi-peer connection recipes.
 */
#pragma once

#include "connection_restore.hpp"
#include "peer_factory.hpp"
#include "settings.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns {

struct online_device_t;

peer_factory_context_t make_factory_context(
    const std::shared_ptr<aseq_t> &aseq,
    const std::shared_ptr<midirouter_t> &router,
    const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns);

std::vector<online_device_t>
collect_online_devices_from_router(const std::shared_ptr<midirouter_t> &router);

peer_id_t ensure_peer_for_identity(const peer_factory_context_t &ctx,
                                   std::shared_ptr<midirouter_t> router,
                                   std::string_view side);

void spawn_import_rtpmidi_connection(const peer_factory_context_t &ctx,
                                     std::shared_ptr<midirouter_t> router,
                                     std::shared_ptr<rtpmidid::rtppeer_t> peer);

peer_id_t spawn_alsa_network_server(const peer_factory_context_t &ctx,
                                    std::shared_ptr<midirouter_t> router,
                                    peer_id_t parent_peer_id,
                                    const std::string &remote_alsa_name);

peer_id_t spawn_alsa_listener_client(const peer_factory_context_t &ctx,
                                   std::shared_ptr<midirouter_t> router,
                                   peer_id_t listener_peer_id,
                                   const std::string &local_portname,
                                   const std::string &hostname,
                                   const std::string &port);

void apply_ini_connects(const peer_factory_context_t &ctx,
                        std::shared_ptr<midirouter_t> router,
                        const std::vector<settings_t::ini_connect_t> &connects);

} // namespace rtpmididns
