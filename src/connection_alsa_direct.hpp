/**
 * Phase 7: pure ALSA-seq ↔ ALSA-seq links (aconnect) + query-side matching.
 */
#pragma once

#include "aseq.hpp"
#include "connection_db.hpp"
#include "device_identity.hpp"
#include "dm_json_status.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rtpmididns {

/** True when @a side is a legacy `alsa:…` or `alsa_seq:…` stored connection side. */
bool is_direct_alsa_side(const std::string &side);

device_identity_t device_identity_from_alsa_port(const alsa_seq_port_row_t &port);

/** Indices into @a ports matching a stored side (query, identity, or legacy id). */
std::vector<size_t> match_alsa_ports_for_side(
    const std::string &side, const std::vector<alsa_seq_port_row_t> &ports);

struct alsa_aconnect_action_t {
  aseq_t::port_t from;
  aseq_t::port_t to;
};

/** Expand saved direct-ALSA connections to concrete directed aconnect actions. */
std::vector<alsa_aconnect_action_t>
plan_alsa_aconnect_actions(const std::vector<stored_connection_t> &saved,
                           const std::vector<alsa_seq_port_row_t> &ports);

} // namespace rtpmididns
