/**
 * Phase 4: device_identity_t derivation from live peer status rows.
 */
#include "device_identity_from_peer.hpp"

#include "peer_kind.hpp"
#include "peer_stable_id.hpp"

#include <utility>

namespace rtpmididns {

namespace {

device_identity_field_t field(std::string key, std::string value) {
  return device_identity_field_t{std::move(key), std::move(value), false};
}

std::optional<device_identity_t>
make_identity(std::string type_prefix,
                std::vector<device_identity_field_t> fields) {
  if (fields.empty())
    return std::nullopt;
  return device_identity_t{std::move(type_prefix), std::move(fields)};
}

std::string peer_display_name(const router_peer_row_t &row) {
  return row.name && !row.name->empty() ? *row.name : std::string{};
}

std::optional<device_identity_t>
identity_device_alsa_seq(const router_peer_row_t &row) {
  if (row.alsa_subscribe_from) {
    const auto &s = *row.alsa_subscribe_from;
    return make_identity("alsa_seq",
                         {field("client", s.client_name), field("port", s.port_name)});
  }
  const auto name = peer_display_name(row);
  if (!name.empty())
    return make_identity("alsa_seq", {field("name", name)});
  return std::nullopt;
}

std::optional<device_identity_t>
identity_device_rawmidi(const router_peer_row_t &row) {
  std::vector<device_identity_field_t> fields;
  if (row.device && !row.device->empty())
    fields.push_back(field("device", *row.device));
  const auto name = peer_display_name(row);
  if (!name.empty())
    fields.push_back(field("name", name));
  return make_identity("rawmidi", std::move(fields));
}

std::optional<device_identity_t>
identity_device_rtpmidi_client(const router_peer_row_t &row) {
  std::vector<device_identity_field_t> fields;
  std::string hostname;
  if (row.connect_hostname && stable_id_is_real_hostname(*row.connect_hostname))
    hostname = *row.connect_hostname;
  else if (row.peer && stable_id_is_real_hostname(row.peer->remote.hostname))
    hostname = row.peer->remote.hostname;

  std::string service;
  if (row.peer && !row.peer->remote.name.empty())
    service = row.peer->remote.name;
  else {
    const auto name = peer_display_name(row);
    if (!name.empty())
      service = name;
  }

  if (!hostname.empty())
    fields.push_back(field("hostname", hostname));
  if (!service.empty())
    fields.push_back(field("service", service));
  if (row.connect_port && !row.connect_port->empty())
    fields.push_back(field("port", *row.connect_port));
  else if (row.port)
    fields.push_back(field("port", std::to_string(*row.port)));

  return make_identity("rtpmidi_client", std::move(fields));
}

std::optional<device_identity_t>
identity_device_rtpmidi_session(const router_peer_row_t &row) {
  std::vector<device_identity_field_t> fields;
  if (row.peer && !row.peer->remote.name.empty() &&
      stable_id_is_real_hostname(row.peer->remote.hostname)) {
    fields.push_back(field("hostname", row.peer->remote.hostname));
    fields.push_back(field("service", row.peer->remote.name));
    return make_identity("rtpmidi_session", std::move(fields));
  }
  const auto name = peer_display_name(row);
  if (!name.empty())
    return make_identity("rtpmidi_session", {field("name", name)});
  return std::nullopt;
}

std::optional<device_identity_t>
identity_export_rtpmidi_server(const router_peer_row_t &row) {
  const auto name = peer_display_name(row);
  if (name.empty())
    return std::nullopt;
  std::vector<device_identity_field_t> fields{field("name", name)};
  if (row.port)
    fields.push_back(field("port", std::to_string(*row.port)));
  return make_identity("rtpmidi_server", std::move(fields));
}

std::optional<device_identity_t>
identity_import_rtpmidi(const router_peer_row_t &row) {
  std::string name;
  if (row.listening && !row.listening->name.empty())
    name = row.listening->name;
  else
    name = peer_display_name(row);
  if (name.empty())
    return std::nullopt;
  std::vector<device_identity_field_t> fields{field("name", name)};
  if (row.listening)
    fields.push_back(field("port", std::to_string(row.listening->midi_port)));
  return make_identity("rtpmidi_multi", std::move(fields));
}

std::optional<device_identity_t>
identity_import_alsa_rtp(const router_peer_row_t &row) {
  const auto peer_name = peer_display_name(row);
  if (peer_name.empty())
    return std::nullopt;
  const auto pos = peer_name.find(" <-> ");
  if (pos != std::string::npos) {
    const std::string remote = peer_name.substr(pos + 5);
    if (!remote.empty())
      return make_identity("alsa_listener", {field("service", remote)});
  }
  return make_identity("alsa_listener", {field("name", peer_name)});
}

std::optional<device_identity_t>
identity_export_alsa_network(const router_peer_row_t &row) {
  const auto name = peer_display_name(row);
  if (name.empty())
    return std::nullopt;
  return make_identity("alsa_multi", {field("name", name)});
}

} // namespace

std::optional<device_identity_t>
compute_device_identity(const router_peer_row_t &row) {
  const auto kind = peer_kind_from_wire_type(row.type.value_or(""));
  if (!kind)
    return std::nullopt;

  switch (*kind) {
  case peer_kind_e::device_alsa_seq:
    return identity_device_alsa_seq(row);
  case peer_kind_e::device_rawmidi:
    return identity_device_rawmidi(row);
  case peer_kind_e::device_rtpmidi_client:
    return identity_device_rtpmidi_client(row);
  case peer_kind_e::device_rtpmidi_session:
    return identity_device_rtpmidi_session(row);
  case peer_kind_e::export_rtpmidi_server:
    return identity_export_rtpmidi_server(row);
  case peer_kind_e::import_rtpmidi:
    return identity_import_rtpmidi(row);
  case peer_kind_e::import_alsa_rtp:
    return identity_import_alsa_rtp(row);
  case peer_kind_e::export_alsa_network:
    return identity_export_alsa_network(row);
  case peer_kind_e::webui_monitor:
  case peer_kind_e::unknown:
    return std::nullopt;
  }
  return std::nullopt;
}

} // namespace rtpmididns
