/**
 * Wire-type and identity-prefix mapping for peer_kind_e.
 */
#include "peer_kind.hpp"

namespace rtpmididns {

const char *peer_kind_wire_type(peer_kind_e kind) {
  switch (kind) {
  case peer_kind_e::device_alsa_seq:
    return "peer_device_alsa_seq_t";
  case peer_kind_e::device_rawmidi:
    return "peer_device_rawmidi_t";
  case peer_kind_e::device_rtpmidi_client:
    return "peer_device_rtpmidi_client_t";
  case peer_kind_e::device_rtpmidi_session:
    return "peer_device_rtpmidi_session_t";
  case peer_kind_e::export_rtpmidi_server:
    return "peer_export_rtpmidi_server_t";
  case peer_kind_e::import_alsa_rtp:
    return "peer_import_alsa_rtp_t";
  case peer_kind_e::import_rtpmidi:
    return "peer_import_rtpmidi_t";
  case peer_kind_e::export_alsa_network:
    return "peer_export_alsa_network_t";
  case peer_kind_e::webui_monitor:
    return "webui_midi_monitor_peer_t";
  case peer_kind_e::unknown:
    return "unknown_peer_t";
  }
  return "unknown_peer_t";
}

const char *peer_kind_identity_prefix(peer_kind_e kind) {
  switch (kind) {
  case peer_kind_e::device_alsa_seq:
    return "alsa_seq";
  case peer_kind_e::device_rawmidi:
    return "rawmidi";
  case peer_kind_e::device_rtpmidi_client:
    return "rtpmidi_client";
  case peer_kind_e::device_rtpmidi_session:
    return "rtpmidi_session";
  case peer_kind_e::export_rtpmidi_server:
    return "rtpmidi_server";
  case peer_kind_e::import_alsa_rtp:
    return "alsa_listener";
  case peer_kind_e::import_rtpmidi:
    return "rtpmidi_multi";
  case peer_kind_e::export_alsa_network:
    return "alsa_multi";
  case peer_kind_e::webui_monitor:
    return "";
  case peer_kind_e::unknown:
    return "";
  }
  return "";
}

std::optional<peer_kind_e> peer_kind_from_identity_prefix(std::string_view prefix) {
  if (prefix == "alsa_seq")
    return peer_kind_e::device_alsa_seq;
  if (prefix == "rawmidi")
    return peer_kind_e::device_rawmidi;
  if (prefix == "rtpmidi_client")
    return peer_kind_e::device_rtpmidi_client;
  if (prefix == "rtpmidi_session")
    return peer_kind_e::device_rtpmidi_session;
  if (prefix == "rtpmidi_server")
    return peer_kind_e::export_rtpmidi_server;
  if (prefix == "alsa_listener")
    return peer_kind_e::import_alsa_rtp;
  if (prefix == "rtpmidi_multi")
    return peer_kind_e::import_rtpmidi;
  if (prefix == "alsa_multi")
    return peer_kind_e::export_alsa_network;
  if (prefix == "webui_monitor")
    return peer_kind_e::webui_monitor;
  return std::nullopt;
}

std::optional<peer_kind_e> peer_kind_from_wire_type(std::string_view wire) {
  if (wire == "peer_device_alsa_seq_t")
    return peer_kind_e::device_alsa_seq;
  if (wire == "peer_device_rawmidi_t")
    return peer_kind_e::device_rawmidi;
  if (wire == "peer_device_rtpmidi_client_t")
    return peer_kind_e::device_rtpmidi_client;
  if (wire == "peer_device_rtpmidi_session_t")
    return peer_kind_e::device_rtpmidi_session;
  if (wire == "peer_export_rtpmidi_server_t")
    return peer_kind_e::export_rtpmidi_server;
  if (wire == "peer_import_alsa_rtp_t")
    return peer_kind_e::import_alsa_rtp;
  if (wire == "peer_import_rtpmidi_t")
    return peer_kind_e::import_rtpmidi;
  if (wire == "peer_export_alsa_network_t")
    return peer_kind_e::export_alsa_network;
  if (wire == "webui_midi_monitor_peer_t")
    return peer_kind_e::webui_monitor;
  return std::nullopt;
}

} // namespace rtpmididns
