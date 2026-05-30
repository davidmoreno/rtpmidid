/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
#include "control_rpc.hpp"
#include "connection_alsa_direct.hpp"
#include "alsa_monitor_tap.hpp"
#include "aseq.hpp"
#include "connection_db.hpp"
#include "connection_restore.hpp"
#include "device_identity.hpp"
#include "device_identity_from_peer.hpp"
#include "device_query.hpp"
#include "device_registry.hpp"
#include "dm_json_generated.hpp"
#include "dm_json_rpc.hpp"
#include "dm_json_status.hpp"
#include "factory.hpp"
#include "peer_device_rawmidi.hpp"
#include "midirouter.hpp"
#include "midipeer.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include "webui_midi_monitor_peer.hpp"
#include <algorithm>
#include <random>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <regex>

namespace rtpmididns {
extern const char *VERSION;
}

namespace rtpmididns {
namespace {

/**
 * Deserialise JSON into T, throwing std::runtime_error on failure.
 * Defined here (after all generated from_json declarations) so that ordinary
 * name lookup finds the right overload without depending on ADL.
 */
template <typename T>
T parse_rpc_params(std::string_view json) {
  T out{};
  if (!dmjson::from_json(json, out))
    throw std::runtime_error("JSON parse error");
  return out;
}

static const std::regex PEER_COMMAND_RE(R"(^(\d+)\.(.*)$)");

static std::string id_json(const dmjson::rpc::envelope_info_t &env) {
  return env.has_id ? env.id_json : "null";
}

// Finish a response by serialising a typed value via the generated to_json.
template <typename T>
static std::string respond(const dmjson::rpc::envelope_info_t &env, const T &value) {
  return dmjson::to_json(rpc_result_t{id_json(env), dmjson::to_json(value)}) + "\n";
}

// Finish a response with a vector of typed values.
template <typename Row>
static std::string respond_rows(const dmjson::rpc::envelope_info_t &env,
                                const std::vector<Row> &rows) {
  dmjson::writer_t arr;
  arr.begin_array();
  for (const auto &r : rows) {
    arr.array_item();
    dmjson::to_json(r, arr);
  }
  arr.end_array();
  std::string arr_json;
  arr.swap_into_string(arr_json);
  return dmjson::to_json(rpc_result_t{id_json(env), std::move(arr_json)}) + "\n";
}

// Finish a response with `["ok"]`.
static std::string respond_ok(const dmjson::rpc::envelope_info_t &env) {
  return dmjson::to_json(rpc_result_t{id_json(env), R"(["ok"])"}) + "\n";
}

// Finish an error response.
static std::string respond_error(const dmjson::rpc::envelope_info_t &env,
                                 std::string_view msg) {
  return dmjson::to_json(rpc_error_envelope_t{id_json(env), std::string(msg)}) + "\n";
}

static mdns_snapshot_t mdns_snapshot(const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns) {
  mdns_snapshot_t o;
  if (!mdns) {
    o.status = "Not available";
    return o;
  }
  o.status = "Available";
  for (auto &a : mdns->announcements) {
    mdns_announce_row_t r;
    r.name = a.name;
    r.port = static_cast<uint32_t>(a.port);
    o.announcements.push_back(std::move(r));
  }
  for (auto &a : mdns->remote_announcements) {
    mdns_remote_row_t r;
    r.name = a.name;
    r.hostname = a.address;
    r.ip = a.ip;
    r.port = static_cast<uint32_t>(a.port);
    o.remote_announcements.push_back(std::move(r));
  }
  return o;
}

static router_create_list_t build_router_create_list() {
  router_create_list_t o;
  o.schemas["local_rawmidi_t"]["name"] = "Name of the peer";
  o.schemas["local_rawmidi_t"]["device"] = "Path to the device";
  o.schemas["peer_device_rtpmidi_client_t"]["name"] = "Name of the peer";
  o.schemas["peer_device_rtpmidi_client_t"]["hostname"] = "Hostname of the server";
  o.schemas["peer_device_rtpmidi_client_t"]["port"] = "Port of the server";
  o.schemas["peer_export_rtpmidi_server_t"]["name"] = "Name of the peer";
  o.schemas["peer_export_rtpmidi_server_t"]["udp_port"] = "UDP port to listen [random]";
  o.schemas["peer_device_alsa_seq_t"]["name"] = "Name of the peer";
  o.schemas["peer_device_alsa_seq_t"]["alsa_client"] =
      "Optional: external ALSA client id to subscribe from";
  o.schemas["peer_device_alsa_seq_t"]["alsa_port"] =
      "Optional: external ALSA port index (with alsa_client)";
  return o;
}

static std::vector<rpc_help_entry_t> build_help_entries() {
  return {
      {"status", "Return status of the daemon"},
      {"router.remove", "Remove a peer from the router"},
      {"router.connect", "Connects two peers at the router. Unidirectional connection."},
      {"router.disconnect", "Disconnects two peers at the router."},
      {"endpoint.connect", "Connect two endpoints (alsa/raw/remote). Server chooses ALSA aconnect vs router peers."},
      {"endpoint.disconnect", "Disconnect two endpoints (alsa/raw/remote)."},
      {"connect", "Connect to a remote RTP-MIDI server (object {hostname, port?, name?})"},
      {"router.create.local_rawmidi", "Create raw MIDI peer"},
      {"router.create.network_rtpmidi_client", "Create RTP-MIDI client peer"},
      {"router.create.network_rtpmidi_listener", "Create RTP-MIDI listener peer"},
      {"router.create.local_alsa_peer", "Create ALSA peer"},
      {"router.create.list", "List create peer schemas"},
      {"mdns.remove", "Delete an mDNS announcement"},
      {"export.rawmidi", "Export a rawmidi device to RTP"},
      {"midi.listAlsaSeq", "List ALSA sequencer ports"},
      {"midi.listAlsaSubscriptions", "List ALSA sequencer subscriptions (aconnect links)"},
      {"midi.listRawMidi", "List raw MIDI devices"},
      {"monitor.start",
       "Start Web UI MIDI monitor for an endpoint id (tees router edges already feeding target)"},
      {"monitor.stop", "Stop a monitor session by uuid"},
      {"connections.list", "List persisted connections (direction, enabled, query sides)"},
      {"connections.save",
       "Save a connection with direction (side_a/side_b: endpoint id or key=value query)"},
      {"connections.add",
       "Add a persisted connection (legacy: both directions, side_a/side_b)"},
      {"connections.remove", "Remove a persisted connection from the database"},
      {"connections.enable", "Enable a persisted connection (reconnect)"},
      {"connections.disable", "Disable a persisted connection (no auto-reconnect)"},
      {"devices.list", "List known devices (online/offline, source, last seen)"},
      {"devices.add_manual",
       "Add a manual device (identity: key=value string, optional display name)"},
      {"devices.remove", "Remove a manual device from the registry"},
      {"help", "Return help text"},
  };
}

enum class endpoint_kind_e { ALSA, RAW, MDNS, HOST, PEER };

struct endpoint_id_t {
  endpoint_kind_e kind;
  // ALSA
  int client = -1;
  int port = -1;
  // RAW
  std::string device;
  // HOST
  std::string hostname;
  std::string hostport;
  // MDNS
  std::string mdns_name;
  int mdns_port = -1;
  // PEER
  uint64_t peer_id = 0;
};

static bool parse_endpoint_id(std::string_view s, endpoint_id_t &out) {
  const std::string str(s);
  if (std::startswith(str, "alsa:")) {
    const auto rest = str.substr(5);
    const auto pos = rest.find(':');
    if (pos == std::string::npos)
      return false;
    try {
      out.kind = endpoint_kind_e::ALSA;
      out.client = std::stoi(rest.substr(0, pos));
      out.port = std::stoi(rest.substr(pos + 1));
      return out.client >= 0 && out.client <= 255 && out.port >= 0 &&
             out.port <= 255;
    } catch (...) {
      return false;
    }
  }
  if (std::startswith(str, "raw:")) {
    out.kind = endpoint_kind_e::RAW;
    out.device = str.substr(4);
    return !out.device.empty();
  }
  if (std::startswith(str, "host:")) {
    out.kind = endpoint_kind_e::HOST;
    const auto rest = str.substr(5);
    const auto pos = rest.rfind(':');
    if (pos == std::string::npos)
      return false;
    out.hostname = rest.substr(0, pos);
    out.hostport = rest.substr(pos + 1);
    return !out.hostname.empty() && !out.hostport.empty();
  }
  if (std::startswith(str, "mdns:")) {
    const auto rest = str.substr(5);
    const auto sep = rest.rfind("::");
    if (sep == std::string::npos)
      return false;
    out.kind = endpoint_kind_e::MDNS;
    out.mdns_name = rest.substr(0, sep);
    try {
      out.mdns_port = std::stoi(rest.substr(sep + 2));
    } catch (...) {
      return false;
    }
    return !out.mdns_name.empty() && out.mdns_port > 0 && out.mdns_port <= 65535;
  }
  if (std::startswith(str, "peer:")) {
    out.kind = endpoint_kind_e::PEER;
    try {
      out.peer_id = static_cast<uint64_t>(std::stoull(str.substr(5)));
      return out.peer_id > 0;
    } catch (...) {
      return false;
    }
  }
  return false;
}

static std::pair<std::string, std::string>
mdns_resolve_to_hostport(const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns,
                         const std::string &name, int port) {
  if (!mdns)
    throw std::runtime_error("mDNS not available");
  for (const auto &a : mdns->remote_announcements) {
    if (a.name != name)
      continue;
    if (static_cast<int>(a.port) != port)
      continue;
    // Prefer the service hostname when present so routing is chosen by the OS
    // (and we avoid binding to a potentially unsuitable resolved IP).
    const std::string host = !a.address.empty() ? a.address : a.ip;
    if (!host.empty())
      return {host, std::to_string(port)};
  }
  throw std::runtime_error("mDNS remote not found");
}

static std::vector<router_peer_row_t>
router_rows_snapshot(const control_rpc_context_t &ctx) {
  return ctx.router ? ctx.router->status_rows()
                    : std::vector<router_peer_row_t>{};
}

static std::optional<peer_id_t>
find_peer_for_alsa(const std::vector<router_peer_row_t> &rows, int client,
                   int port) {
  const std::string web_name = FMT::format("WEB:ALSA:{}:{}", client, port);
  for (const auto &r : rows) {
    if (r.type.value_or("") != "peer_device_alsa_seq_t")
      continue;
    const auto id = static_cast<peer_id_t>(r.id.value_or(0));
    if (id == 0)
      continue;
    if (r.name && *r.name == web_name)
      return id;
    if (!r.alsa_subscribe_from)
      continue;
    if (r.alsa_subscribe_from->client == client &&
        r.alsa_subscribe_from->port == port) {
      return id;
    }
  }
  return std::nullopt;
}

static std::optional<peer_id_t>
find_peer_for_raw(const std::vector<router_peer_row_t> &rows,
                  const std::string &device) {
  for (const auto &r : rows) {
    if (r.type.value_or("") != "peer_device_rawmidi_t")
      continue;
    if (r.device && *r.device == device)
      return static_cast<peer_id_t>(r.id.value_or(0));
  }
  return std::nullopt;
}

static std::optional<peer_id_t>
find_peer_for_host(const std::vector<router_peer_row_t> &rows,
                   const std::string &hostname, const std::string &port) {
  const auto host_matches = [&](const std::string &candidate) {
    return !candidate.empty() && candidate == hostname;
  };
  for (const auto &r : rows) {
    if (r.type.value_or("") != "peer_device_rtpmidi_client_t")
      continue;
    const auto ch = r.connect_hostname.value_or("");
    const auto cp = r.connect_port.value_or("");
    const auto id = static_cast<peer_id_t>(r.id.value_or(0));
    if (id == 0)
      continue;
    if (cp == port && host_matches(ch))
      return id;
    if (cp == port && r.peer && host_matches(r.peer->remote.hostname))
      return id;
  }
  return std::nullopt;
}

static peer_id_t ensure_peer_for_endpoint(control_rpc_context_t &ctx,
                                         const endpoint_id_t &eid,
                                         const std::vector<router_peer_row_t> &rows) {
  if (!ctx.router)
    throw std::runtime_error("router not available");
  if (eid.kind == endpoint_kind_e::PEER)
    return static_cast<peer_id_t>(eid.peer_id);
  if (eid.kind == endpoint_kind_e::ALSA) {
    const auto found = find_peer_for_alsa(rows, eid.client, eid.port);
    if (found && *found != 0)
      return *found;
    if (!ctx.aseq)
      throw std::runtime_error("ALSA sequencer not available");
    const std::string name = FMT::format("WEB:ALSA:{}:{}", eid.client, eid.port);
    auto peer = make_peer_device_alsa_seq(name, ctx.aseq, eid.client, eid.port);
    return ctx.router->add_peer(peer);
  }
  if (eid.kind == endpoint_kind_e::RAW) {
    const auto found = find_peer_for_raw(rows, eid.device);
    if (found && *found != 0)
      return *found;
    const std::string name = FMT::format("WEB:RAW:{}", eid.device);
    auto peer = make_peer_device_rawmidi(name, eid.device);
    return ctx.router->add_peer(peer);
  }
  if (eid.kind == endpoint_kind_e::MDNS) {
    const auto hp = mdns_resolve_to_hostport(ctx.mdns, eid.mdns_name, eid.mdns_port);
    endpoint_id_t host{};
    host.kind = endpoint_kind_e::HOST;
    host.hostname = hp.first;
    host.hostport = hp.second;
    return ensure_peer_for_endpoint(ctx, host, rows);
  }
  if (eid.kind == endpoint_kind_e::HOST) {
    const auto found = find_peer_for_host(rows, eid.hostname, eid.hostport);
    if (found && *found != 0)
      return *found;
    const std::string nm = FMT::format("WEB · {}", eid.hostname);
    auto peer = make_peer_device_rtpmidi_client(nm, eid.hostname, eid.hostport);
    return ctx.router->add_peer(peer);
  }
  throw std::runtime_error("Unknown endpoint kind");
}

static std::string random_uuid_v4() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<unsigned> dis(0, 255);
  uint8_t b[16];
  for (auto &x : b)
    x = static_cast<uint8_t>(dis(gen));
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);
  static const char *const hex = "0123456789abcdef";
  std::string u;
  u.reserve(36);
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      u += '-';
    u += hex[b[i] >> 4];
    u += hex[b[i] & 0xF];
  }
  return u;
}

/**
 * Wire monitor sink so it receives the same MIDI as `target` peer:
 * - Incoming to target: for each existing router edge `from -> target`, add
 *   `from -> monitor` (duplicate packets destined for `target`).
 * - Outgoing from target: add `target -> monitor` so anything `target` sends
 *   to its destinations is also sent to the monitor (covers endpoints that act
 *   as sources only — previously only incoming edges were teed, so keyboard /
 *   RTP-export peers often had no matching edges).
 */
static std::string escape_stable_component(std::string s) {
  for (char &c : s) {
    if (c == ':')
      c = '|';
  }
  return s;
}

static std::string build_stable_id(std::string prefix,
                                   const std::vector<std::string> &parts) {
  std::string out = std::move(prefix);
  for (const auto &p : parts) {
    out += ':';
    out += escape_stable_component(p);
  }
  return out;
}

static bool is_alsa_numeric_endpoint(std::string_view s) {
  if (!std::startswith(s, "alsa:"))
    return false;
  const auto rest = s.substr(5);
  const auto pos = rest.find(':');
  if (pos == std::string::npos)
    return false;
  const auto is_digits = [](std::string_view x) {
    return !x.empty() &&
           std::all_of(x.begin(), x.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
  };
  return is_digits(rest.substr(0, pos)) && is_digits(rest.substr(pos + 1));
}

static std::vector<online_device_t>
online_devices_snapshot(const control_rpc_context_t &ctx) {
  std::vector<online_device_t> out;
  for (const auto &row : router_rows_snapshot(ctx)) {
    if (!row.id)
      continue;
    const auto identity = compute_device_identity(row);
    if (!identity)
      continue;
    online_device_t device;
    device.peer_id = static_cast<peer_id_t>(*row.id);
    device.identity = *identity;
    device.legacy_stable_id = compute_stable_id(row);
    out.push_back(std::move(device));
  }
  return out;
}

struct side_match_info_t {
  bool active = false;
  std::optional<uint64_t> peer_id;
};

static side_match_info_t match_connection_side(
    const std::string &side, const std::vector<online_device_t> &online) {
  side_match_info_t info;
  const auto indices = match_side_to_devices(side, online);
  if (indices.empty())
    return info;
  info.active = true;
  info.peer_id = static_cast<uint64_t>(online[indices.front()].peer_id);
  return info;
}

static std::optional<std::string>
identity_from_alsa_names(const std::string &client_name,
                         const std::string &port_name) {
  device_identity_t id;
  id.type_prefix = "alsa_seq";
  id.fields.push_back(device_identity_field_t{"client", client_name, false});
  id.fields.push_back(device_identity_field_t{"port", port_name, false});
  return id.serialize();
}

static std::optional<std::string>
identity_from_raw_device(const std::string &device) {
  device_identity_t id;
  id.type_prefix = "rawmidi";
  id.fields.push_back(device_identity_field_t{"device", device, false});
  return id.serialize();
}

static std::optional<std::string>
identity_from_rtpmidi_remote(const std::string &hostname,
                             const std::string &service) {
  device_identity_t id;
  id.type_prefix = "rtpmidi_client";
  id.fields.push_back(device_identity_field_t{"hostname", hostname, false});
  id.fields.push_back(device_identity_field_t{"service", service, false});
  return id.serialize();
}

static std::optional<std::string>
resolve_side_to_stable_id(control_rpc_context_t &ctx, const std::string &side,
                          const std::vector<router_peer_row_t> &rows);

static std::optional<std::string>
resolve_side_to_connection_side(control_rpc_context_t &ctx,
                                const std::string &side,
                                const std::vector<router_peer_row_t> &rows) {
  if (device_query_t::parse(side) || device_identity_t::parse(side))
    return side;

  endpoint_id_t eid{};
  if (parse_endpoint_id(side, eid)) {
    if (eid.kind == endpoint_kind_e::PEER) {
      for (const auto &r : rows) {
        if (!r.id || static_cast<uint64_t>(*r.id) != eid.peer_id)
          continue;
        const auto did = compute_device_identity(r);
        if (did)
          return did->serialize();
        const auto sid = compute_stable_id(r);
        if (!sid)
          throw std::runtime_error(
              FMT::format("Peer {} has no stable id yet", eid.peer_id));
        return sid;
      }
      throw std::runtime_error(FMT::format("Unknown peer {}", eid.peer_id));
    }
    if (eid.kind == endpoint_kind_e::ALSA) {
      if (!ctx.aseq)
        throw std::runtime_error("ALSA sequencer not available");
      const auto cn = ctx.aseq->get_client_name_by_id(eid.client);
      const auto pn = ctx.aseq->get_port_name(eid.client, eid.port);
      if (cn.empty() || pn.empty())
        throw std::runtime_error("Could not resolve ALSA client/port names");
      if (auto id = identity_from_alsa_names(cn, pn))
        return id;
      return build_stable_id("alsa", {cn, pn});
    }
    if (eid.kind == endpoint_kind_e::RAW) {
      if (eid.device.empty())
        throw std::runtime_error("Empty raw MIDI device");
      if (auto id = identity_from_raw_device(eid.device))
        return id;
      return build_stable_id("rawmidi", {eid.device});
    }
    if (eid.kind == endpoint_kind_e::MDNS) {
      const auto hp =
          mdns_resolve_to_hostport(ctx.mdns, eid.mdns_name, eid.mdns_port);
      if (hp.first.empty() || eid.mdns_name.empty())
        throw std::runtime_error("Could not resolve mDNS service");
      if (auto id = identity_from_rtpmidi_remote(hp.first, eid.mdns_name))
        return id;
      return build_stable_id("rtpmidi", {hp.first, eid.mdns_name});
    }
    if (eid.kind == endpoint_kind_e::HOST) {
      const auto found = find_peer_for_host(rows, eid.hostname, eid.hostport);
      if (found && *found != 0) {
        for (const auto &r : rows) {
          if (r.id && static_cast<peer_id_t>(*r.id) == *found) {
            if (const auto did = compute_device_identity(r))
              return did->serialize();
            if (const auto sid = compute_stable_id(r))
              return sid;
            break;
          }
        }
      }
      if (eid.hostname.empty())
        throw std::runtime_error("Empty hostname");
      if (auto id = identity_from_rtpmidi_remote(eid.hostname, eid.hostname))
        return id;
      return build_stable_id("rtpmidi", {eid.hostname, eid.hostname});
    }
    throw std::runtime_error("Unknown endpoint kind");
  }

  return resolve_side_to_stable_id(ctx, side, rows);
}

static std::string device_source_wire(device_source_e source) {
  switch (source) {
  case device_source_e::discovered:
    return "discovered";
  case device_source_e::ini:
    return "ini";
  case device_source_e::manual:
    return "manual";
  }
  return "discovered";
}

static std::optional<std::string>
resolve_side_to_stable_id(control_rpc_context_t &ctx, const std::string &side,
                          const std::vector<router_peer_row_t> &rows) {
  endpoint_id_t eid{};
  if (parse_endpoint_id(side, eid)) {
    if (eid.kind == endpoint_kind_e::PEER) {
      for (const auto &r : rows) {
        if (!r.id || static_cast<uint64_t>(*r.id) != eid.peer_id)
          continue;
        const auto sid = compute_stable_id(r);
        if (!sid)
          throw std::runtime_error(
              FMT::format("Peer {} has no stable id yet", eid.peer_id));
        return sid;
      }
      throw std::runtime_error(FMT::format("Unknown peer {}", eid.peer_id));
    }
    if (eid.kind == endpoint_kind_e::ALSA) {
      if (!ctx.aseq)
        throw std::runtime_error("ALSA sequencer not available");
      const auto cn = ctx.aseq->get_client_name_by_id(eid.client);
      const auto pn = ctx.aseq->get_port_name(eid.client, eid.port);
      if (cn.empty() || pn.empty())
        throw std::runtime_error("Could not resolve ALSA client/port names");
      return build_stable_id("alsa", {cn, pn});
    }
    if (eid.kind == endpoint_kind_e::RAW) {
      if (eid.device.empty())
        throw std::runtime_error("Empty raw MIDI device");
      return build_stable_id("rawmidi", {eid.device});
    }
    if (eid.kind == endpoint_kind_e::MDNS) {
      const auto hp =
          mdns_resolve_to_hostport(ctx.mdns, eid.mdns_name, eid.mdns_port);
      if (hp.first.empty() || eid.mdns_name.empty())
        throw std::runtime_error("Could not resolve mDNS service");
      return build_stable_id("rtpmidi", {hp.first, eid.mdns_name});
    }
    if (eid.kind == endpoint_kind_e::HOST) {
      const auto found = find_peer_for_host(rows, eid.hostname, eid.hostport);
      if (found && *found != 0) {
        for (const auto &r : rows) {
          if (r.id && static_cast<peer_id_t>(*r.id) == *found) {
            const auto sid = compute_stable_id(r);
            if (sid)
              return sid;
            break;
          }
        }
      }
      if (eid.hostname.empty())
        throw std::runtime_error("Empty hostname");
      return build_stable_id("rtpmidi", {eid.hostname, eid.hostname});
    }
    throw std::runtime_error("Unknown endpoint kind");
  }

  /* Accept any well-formed stable id of the form `<prefix>:<rest>` where
     <prefix> is `[a-z][a-z0-9_]*` and <rest> is non-empty. This intentionally
     replaces the previous hardcoded allowlist of known prefixes - we keep
     adding new ones in compute_stable_id (alsa_local, rawmidi_named,
     rtpmidi_client_named, rtpmidi_in_named, alsa_listener_named, plus a
     generic `<short_type>:<peer_name>` last-resort), and the allowlist kept
     drifting out of sync. The `is_alsa_numeric_endpoint` exclusion stays so
     `alsa:128:0` is still routed through parse_endpoint_id above. */
  if (!is_alsa_numeric_endpoint(side)) {
    const auto colon = side.find(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < side.size()) {
      bool prefix_ok = true;
      for (size_t i = 0; i < colon; i++) {
        const char c = side[i];
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (i > 0 && ((c >= '0' && c <= '9') || c == '_'));
        if (!ok) {
          prefix_ok = false;
          break;
        }
      }
      if (prefix_ok)
        return side;
    }
  }

  throw std::runtime_error(
      FMT::format("Could not resolve stable id for side '{}'", side));
}

static void maybe_persist_direct_endpoint_connection(
    control_rpc_context_t &ctx, const std::string &from_endpoint,
    const std::string &to_endpoint, bool bidi,
    const std::vector<router_peer_row_t> &rows) {
  if (!ctx.connection_db)
    return;
  const auto sa = resolve_side_to_connection_side(ctx, from_endpoint, rows);
  const auto sb = resolve_side_to_connection_side(ctx, to_endpoint, rows);
  if (!sa || !sb || !is_direct_alsa_side(*sa) || !is_direct_alsa_side(*sb))
    return;
  stored_connection_t row;
  row.side_a = *sa;
  row.side_b = *sb;
  row.direction =
      bidi ? connection_direction_e::both : connection_direction_e::a2b;
  row.enabled = true;
  ctx.connection_db->save_stored_connection(std::move(row));
}

static void tee_monitor_edges(control_rpc_context_t &ctx, peer_id_t target,
                              peer_id_t monitor_id,
                              const std::shared_ptr<webui_midi_monitor_peer_t> &mon) {
  const auto rows = router_rows_snapshot(ctx);
  unsigned incoming_tees = 0;
  for (const auto &r : rows) {
    if (!r.id || !r.send_to)
      continue;
    const peer_id_t from = static_cast<peer_id_t>(*r.id);
    if (from == monitor_id)
      continue;
    for (uint32_t to_raw : *r.send_to) {
      const peer_id_t to = static_cast<peer_id_t>(to_raw);
      if (to != target)
        continue;
      ctx.router->connect_blocking(from, monitor_id);
      incoming_tees++;
      break;
    }
  }

  ctx.router->connect_blocking(target, monitor_id);

  INFO("monitor.start tee target_peer={} monitor_peer={}: {} incoming duplicate "
       "edge(s) (from→monitor when from→target existed); always added "
       "outgoing target_peer→monitor_peer",
       target, monitor_id, incoming_tees);
}

} // namespace

std::string control_rpc_dispatch_line(control_rpc_context_t &ctx, std::string_view line_in) {
  dmjson::rpc::envelope_info_t env;
  std::string parse_err;
  const std::string buf = trim_copy(std::string(line_in));
  if (!dmjson::rpc::scan_envelope(buf, env, parse_err))
    return dmjson::to_json(rpc_error_body_t{parse_err}) + "\n";

  const std::string_view params =
      env.has_params ? env.params_json : std::string_view("{}");

  try {
    if (env.method == "status") {
      daemon_status_t ds;
      ds.version = VERSION;
      ds.settings.alsa_name = settings.alsa_name;
      ds.settings.control_filename = settings.control_filename;
      ds.router = ctx.router->status_rows();
      ds.mdns = mdns_snapshot(ctx.mdns);
      return respond(env, ds);
    }
    if (env.method == "router.remove") {
      auto p = parse_rpc_params<router_remove_params_t>(params);
      ctx.router->enqueue_remove_peer(static_cast<peer_id_t>(p.peer_id));
      return respond_ok(env);
    }
    if (env.method == "router.connect") {
      auto p = parse_rpc_params<router_connect_params_t>(params);
      ctx.router->enqueue_connect(static_cast<peer_id_t>(p.from),
                                  static_cast<peer_id_t>(p.to));
      return respond_ok(env);
    }
    if (env.method == "router.disconnect") {
      auto p = parse_rpc_params<router_connect_params_t>(params);
      ctx.router->enqueue_disconnect(static_cast<peer_id_t>(p.from),
                                     static_cast<peer_id_t>(p.to));
      return respond_ok(env);
    }
    if (env.method == "endpoint.connect") {
      auto p = parse_rpc_params<endpoint_connect_params_t>(params);
      endpoint_id_t a{};
      endpoint_id_t b{};
      if (!parse_endpoint_id(p.from, a) || !parse_endpoint_id(p.to, b))
        throw std::runtime_error("Need {from,to} endpoint ids");
      const bool bidi = p.bidi.value_or(true);

      if (a.kind == endpoint_kind_e::ALSA && b.kind == endpoint_kind_e::ALSA) {
        if (!ctx.aseq)
          throw std::runtime_error("ALSA sequencer not available");
        const aseq_t::port_t from{static_cast<uint8_t>(a.client),
                                  static_cast<uint8_t>(a.port)};
        const aseq_t::port_t to{static_cast<uint8_t>(b.client),
                                static_cast<uint8_t>(b.port)};
        ctx.aseq->connect_external(from, to);
        if (bidi)
          ctx.aseq->connect_external(to, from);
        const auto rows = router_rows_snapshot(ctx);
        maybe_persist_direct_endpoint_connection(ctx, p.from, p.to, bidi, rows);
        return respond_ok(env);
      }

      const auto ensure = [&](const endpoint_id_t &eid) {
        const auto snap = router_rows_snapshot(ctx);
        return ensure_peer_for_endpoint(ctx, eid, snap);
      };
      const auto pa = ensure(a);
      const auto pb = ensure(b);
      ctx.router->connect_blocking(pa, pb);
      if (bidi)
        ctx.router->connect_blocking(pb, pa);
      return respond_ok(env);
    }
    if (env.method == "endpoint.disconnect") {
      auto p = parse_rpc_params<endpoint_disconnect_params_t>(params);
      endpoint_id_t a{};
      endpoint_id_t b{};
      if (!parse_endpoint_id(p.from, a) || !parse_endpoint_id(p.to, b))
        throw std::runtime_error("Need {from,to} endpoint ids");

      if (a.kind == endpoint_kind_e::ALSA && b.kind == endpoint_kind_e::ALSA) {
        if (!ctx.aseq)
          throw std::runtime_error("ALSA sequencer not available");
        const aseq_t::port_t from{static_cast<uint8_t>(a.client),
                                  static_cast<uint8_t>(a.port)};
        const aseq_t::port_t to{static_cast<uint8_t>(b.client),
                                static_cast<uint8_t>(b.port)};
        ctx.aseq->disconnect_external(from, to);
        ctx.aseq->disconnect_external(to, from);
        return respond_ok(env);
      }

      const auto rows = router_rows_snapshot(ctx);
      const auto pa = ensure_peer_for_endpoint(ctx, a, rows);
      const auto pb = ensure_peer_for_endpoint(ctx, b, rows);
      ctx.router->enqueue_disconnect(pa, pb);
      ctx.router->enqueue_disconnect(pb, pa);
      return respond_ok(env);
    }
    if (env.method == "connect") {
      auto p = parse_rpc_params<connect_params_t>(params);
      if (p.hostname.empty())
        throw std::runtime_error("Need object {hostname, port?, name?}");
      ctx.router->add_peer(make_peer_import_alsa_rtp(
          ctx.router, p.name.value_or(p.hostname), p.hostname,
          p.port.value_or("5004"), ctx.aseq, "0"));
      return respond_ok(env);
    }
    if (env.method == "router.create.local_rawmidi") {
      auto p = parse_rpc_params<create_local_rawmidi_params_t>(params);
      auto peer = make_peer_device_rawmidi(p.name, p.device);
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_client") {
      auto p = parse_rpc_params<create_network_rtpmidi_client_params_t>(params);
      auto peer = make_peer_device_rtpmidi_client(p.name, p.hostname, p.port);
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_listener") {
      auto p = parse_rpc_params<create_network_rtpmidi_listener_params_t>(params);
      auto peer = make_peer_export_rtpmidi_server(p.name, std::to_string(p.udp_port));
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.local_alsa_peer") {
      auto p = parse_rpc_params<create_local_alsa_peer_params_t>(params);
      auto peer = (p.alsa_client && p.alsa_port)
                      ? make_peer_device_alsa_seq(p.name, ctx.aseq, *p.alsa_client, *p.alsa_port)
                      : make_peer_device_alsa_seq(p.name, ctx.aseq);
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.list")
      return respond(env, build_router_create_list());
    if (env.method == "mdns.remove") {
      if (!ctx.mdns)
        throw std::runtime_error("mDNS not available");
      auto p = parse_rpc_params<mdns_remove_params_t>(params);
      ctx.mdns->remove_announcement(p.name, p.hostname.value_or(""), p.port);
      return respond_ok(env);
    }
    if (env.method == "export.rawmidi") {
      auto p = parse_rpc_params<export_rawmidi_params_t>(params);
      if (p.device.empty()) {
        export_rawmidi_error_t err;
        err.error = "Need device";
        err.params["device"] = "Path to the device. Mandatory.";
        err.params["name"] = "Name of the peer";
        err.params["local_udp_port"] = "Local UDP port";
        err.params["remote_udp_port"] = "Remote UDP port";
        err.params["hostname"] = "Hostname of the server if want to connect to. Else is a local listener.";
        return respond(env, err);
      }
      rtpmididns::settings_t::rawmidi_t rawmidi;
      rawmidi.device = p.device;
      rawmidi.name = p.name.value_or("");
      rawmidi.local_udp_port = p.local_udp_port.value_or("0");
      rawmidi.remote_udp_port = p.remote_udp_port.value_or("0");
      rawmidi.hostname = p.hostname.value_or("");
      create_rawmidi_rtpclient_pair(ctx.router.get(), rawmidi);
      return respond_ok(env);
    }
    if (env.method == "midi.listAlsaSeq") {
      if (!ctx.aseq)
        return respond(env, rpc_error_body_t{"ALSA sequencer not available"});
      return respond_rows(env, ctx.aseq->enumerate_exported_ports());
    }
    if (env.method == "midi.listAlsaSubscriptions") {
      if (!ctx.aseq)
        return respond(env, rpc_error_body_t{"ALSA sequencer not available"});
      return respond_rows(env, ctx.aseq->enumerate_subscriptions());
    }
    if (env.method == "midi.listRawMidi")
      return respond_rows(env, enumerate_rawmidi_devices());
    if (env.method == "monitor.start") {
      if (!ctx.router)
        throw std::runtime_error("router not available");
      auto p = parse_rpc_params<monitor_start_params_t>(params);
      endpoint_id_t eid{};
      if (!parse_endpoint_id(p.endpoint, eid))
        throw std::runtime_error("Bad endpoint id");
      const auto rows = router_rows_snapshot(ctx);
      const peer_id_t target = ensure_peer_for_endpoint(ctx, eid, rows);
      const std::string uuid = random_uuid_v4();
      auto peer_base = make_webui_midi_monitor_peer(uuid, target);
      auto mon =
          std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(peer_base);
      if (!mon)
        throw std::runtime_error("internal: monitor peer");
      const peer_id_t mid = ctx.router->add_peer(peer_base);
      monitor_registry_register(uuid, mon);
      tee_monitor_edges(ctx, target, mid, mon);
      if (eid.kind == endpoint_kind_e::ALSA && ctx.aseq) {
        setup_alsa_monitor_taps(
            ctx.aseq, ctx.router, static_cast<uint8_t>(eid.client),
            static_cast<uint8_t>(eid.port), target, mid, uuid,
            [&](uint8_t client, uint8_t port) {
              endpoint_id_t ep{};
              ep.kind = endpoint_kind_e::ALSA;
              ep.client = client;
              ep.port = port;
              const auto snap = router_rows_snapshot(ctx);
              return ensure_peer_for_endpoint(ctx, ep, snap);
            });
      }
      monitor_start_result_t out{};
      out.uuid = uuid;
      out.peer_id = static_cast<uint64_t>(mid);
      out.target_peer_id = static_cast<uint64_t>(target);
      return respond(env, out);
    }
    if (env.method == "monitor.stop") {
      if (!ctx.router)
        throw std::runtime_error("router not available");
      auto p = parse_rpc_params<monitor_stop_params_t>(params);
      auto mon = monitor_registry_lookup(p.uuid);
      if (!mon)
        throw std::runtime_error("Unknown monitor session");
      teardown_alsa_monitor_taps(ctx.router, p.uuid);
      monitor_session_stop(ctx.router, mon);
      return respond_ok(env);
    }
    if (env.method == "connections.list") {
      connections_list_result_t out{};
      if (!ctx.connection_db) {
        return respond(env, out);
      }
      out.enabled = 1;
      const auto online = online_devices_snapshot(ctx);
      for (const auto &p : ctx.connection_db->database().list_connections()) {
        persisted_connection_row_t row;
        row.side_a = p.side_a;
        row.side_b = p.side_b;
        row.direction = connection_direction_to_wire(p.direction);
        row.enabled = p.enabled ? 1 : 0;
        const auto ma = match_connection_side(p.side_a, online);
        const auto mb = match_connection_side(p.side_b, online);
        row.active_a = ma.active ? 1 : 0;
        row.active_b = mb.active ? 1 : 0;
        row.peer_a = ma.peer_id;
        row.peer_b = mb.peer_id;
        out.connections.push_back(std::move(row));
      }
      return respond(env, out);
    }
    if (env.method == "connections.save") {
      if (!ctx.connection_db)
        throw std::runtime_error("Connection database is not enabled");
      auto p = parse_rpc_params<connections_save_params_t>(params);
      const auto rows = router_rows_snapshot(ctx);
      const auto sa = resolve_side_to_connection_side(ctx, p.side_a, rows);
      const auto sb = resolve_side_to_connection_side(ctx, p.side_b, rows);
      if (!sa || !sb)
        throw std::runtime_error("Could not resolve connection sides");
      if (*sa == *sb)
        throw std::runtime_error("Both sides resolve to the same identity");
      stored_connection_t row;
      row.side_a = *sa;
      row.side_b = *sb;
      row.direction = connection_direction_from_wire(p.direction);
      row.enabled = p.enabled != 0;
      ctx.connection_db->save_stored_connection(std::move(row));
      return respond_ok(env);
    }
    if (env.method == "connections.add") {
      if (!ctx.connection_db)
        throw std::runtime_error("Connection database is not enabled");
      auto p = parse_rpc_params<connections_mutate_params_t>(params);
      const auto rows = router_rows_snapshot(ctx);
      const auto sa = resolve_side_to_connection_side(ctx, p.side_a, rows);
      const auto sb = resolve_side_to_connection_side(ctx, p.side_b, rows);
      if (!sa || !sb)
        throw std::runtime_error("Could not resolve stable ids");
      if (*sa == *sb)
        throw std::runtime_error("Both sides resolve to the same stable id");
      stored_connection_t row;
      row.side_a = *sa;
      row.side_b = *sb;
      row.direction = connection_direction_e::both;
      row.enabled = true;
      ctx.connection_db->save_stored_connection(std::move(row));
      return respond_ok(env);
    }
    if (env.method == "connections.remove") {
      if (!ctx.connection_db)
        throw std::runtime_error("Connection database is not enabled");
      auto p = parse_rpc_params<connections_mutate_params_t>(params);
      const auto rows = router_rows_snapshot(ctx);
      const auto sa = resolve_side_to_connection_side(ctx, p.side_a, rows);
      const auto sb = resolve_side_to_connection_side(ctx, p.side_b, rows);
      if (!sa || !sb)
        throw std::runtime_error("Could not resolve stable ids");
      ctx.connection_db->remove_stable_pair(*sa, *sb);
      return respond_ok(env);
    }
    if (env.method == "connections.enable" ||
        env.method == "connections.disable") {
      if (!ctx.connection_db)
        throw std::runtime_error("Connection database is not enabled");
      auto p = parse_rpc_params<connections_enable_params_t>(params);
      const auto rows = router_rows_snapshot(ctx);
      const auto sa = resolve_side_to_connection_side(ctx, p.side_a, rows);
      const auto sb = resolve_side_to_connection_side(ctx, p.side_b, rows);
      if (!sa || !sb)
        throw std::runtime_error("Could not resolve connection sides");
      const bool ok = ctx.connection_db->set_stored_enabled(
          *sa, *sb, env.method == "connections.enable");
      if (!ok)
        throw std::runtime_error("Connection not found in database");
      return respond_ok(env);
    }
    if (env.method == "devices.list") {
      devices_list_result_t out{};
      if (!ctx.device_registry)
        return respond(env, out);
      out.enabled = 1;
      for (const auto &d : ctx.device_registry->list_devices()) {
        device_list_row_t row;
        row.identity = d.identity_key();
        row.type = d.identity.type_prefix;
        row.name = d.display_name.empty() ? d.identity.type_prefix : d.display_name;
        for (const auto &f : d.identity.fields) {
          if (f.key == "name" || f.key == "service" || f.key == "client") {
            if (!f.value.empty())
              row.name = f.value;
          }
        }
        row.source = device_source_wire(d.source);
        row.first_seen = d.first_seen;
        row.last_seen = d.last_seen;
        row.online = d.online() ? 1 : 0;
        if (d.online_peer_id)
          row.peer_id = static_cast<uint64_t>(*d.online_peer_id);
        out.devices.push_back(std::move(row));
      }
      return respond(env, out);
    }
    if (env.method == "devices.add_manual") {
      if (!ctx.device_registry)
        throw std::runtime_error("Device registry is not enabled");
      auto p = parse_rpc_params<devices_add_manual_params_t>(params);
      const auto id = device_identity_t::parse(p.identity);
      if (!id)
        throw std::runtime_error("Invalid device identity (expected key=value form)");
      std::string display = p.name.value_or("");
      ctx.device_registry->add_manual(*id, display);
      return respond_ok(env);
    }
    if (env.method == "devices.remove") {
      if (!ctx.device_registry)
        throw std::runtime_error("Device registry is not enabled");
      auto p = parse_rpc_params<devices_remove_params_t>(params);
      if (!ctx.device_registry->remove_manual(p.identity))
        throw std::runtime_error("Manual device not found (only manual entries can be removed)");
      return respond_ok(env);
    }
    if (env.method == "help")
      return respond_rows(env, build_help_entries());

    std::smatch match;
    if (std::regex_match(env.method, match, PEER_COMMAND_RE)) {
      const auto peer_id = static_cast<peer_id_t>(std::stoul(match[1].str()));
      auto peer = ctx.router->get_peer_by_id(peer_id);
      if (!peer)
        return respond_error(env, FMT::format("Unknown peer '{}'", peer_id));
      std::string perr;
      dmjson::writer_t inner;
      if (!peer->control_peer_command(match[2].str(), params, inner, perr))
        return respond_error(env, perr);
      return dmjson::to_json(rpc_result_t{id_json(env), std::string(inner.view())}) + "\n";
    }

    return respond_error(env, FMT::format("Unknown method '{}'", env.method));
  } catch (const std::exception &e) {
    return respond_error(env, e.what());
  }
}

} // namespace rtpmididns
