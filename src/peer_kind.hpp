/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 */
#pragma once

#include <optional>
#include <string_view>

namespace rtpmididns {

/**
 * Canonical peer classification. C++ class names are not used on the wire or
 * in the connection database — map through this enum instead.
 */
enum class peer_kind_e {
  device_alsa_seq,
  device_rawmidi,
  device_rtpmidi_client,
  device_rtpmidi_session,
  export_alsa_network,
  export_rtpmidi_server,
  import_rtpmidi,
  import_alsa_rtp,
  webui_monitor,
  unknown,
};

/** Status / control / DB wire type. */
const char *peer_kind_wire_type(peer_kind_e kind);

/** Primary device-identity type prefix for Phase 2 (`key=value` grammar). */
const char *peer_kind_identity_prefix(peer_kind_e kind);

/** `router.create.*` schema key when this kind is user-instantiable. */
std::optional<const char *>
peer_kind_rpc_create_key(peer_kind_e kind);

std::optional<peer_kind_e> peer_kind_from_wire_type(std::string_view wire);

bool peer_kind_is_device(peer_kind_e kind);
bool peer_kind_is_export(peer_kind_e kind);
bool peer_kind_is_import(peer_kind_e kind);

} // namespace rtpmididns
