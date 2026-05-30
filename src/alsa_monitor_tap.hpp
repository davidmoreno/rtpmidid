/**
 * Phase 7: transient router taps for monitoring pure-ALSA (aconnect) paths.
 */
#pragma once

#include "aseq.hpp"
#include "midirouter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rtpmididns {

/**
 * While a monitor session is open, duplicate MIDI from aconnect sources that feed
 * the monitored ALSA port onto the monitor peer (without inserting router edges
 * between source and target — the direct aconnect link stays intact).
 *
 * @a ensure_alsa_peer materializes a router peer subscribed to (client, port).
 */
void setup_alsa_monitor_taps(
    const std::shared_ptr<aseq_t> &aseq,
    const std::shared_ptr<midirouter_t> &router,
    uint8_t target_client, uint8_t target_port, peer_id_t target_peer,
    peer_id_t monitor_peer, const std::string &session_uuid,
    const std::function<peer_id_t(uint8_t client, uint8_t port)> &ensure_alsa_peer);

void teardown_alsa_monitor_taps(const std::shared_ptr<midirouter_t> &router,
                                const std::string &session_uuid);

} // namespace rtpmididns
