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
#include "aseq.hpp"
#include "dm_json_generated.hpp"
#include "dm_json_rpc.hpp"
#include "dm_json_status.hpp"
#include "factory.hpp"
#include "local_rawmidi_peer.hpp"
#include "midirouter.hpp"
#include "midipeer.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
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
  o.schemas["network_rtpmidi_client_t"]["name"] = "Name of the peer";
  o.schemas["network_rtpmidi_client_t"]["hostname"] = "Hostname of the server";
  o.schemas["network_rtpmidi_client_t"]["port"] = "Port of the server";
  o.schemas["network_rtpmidi_listener_t"]["name"] = "Name of the peer";
  o.schemas["network_rtpmidi_listener_t"]["udp_port"] = "UDP port to listen [random]";
  o.schemas["local_alsa_peer_t"]["name"] = "Name of the peer";
  o.schemas["local_alsa_peer_t"]["alsa_client"] =
      "Optional: external ALSA client id to subscribe from";
  o.schemas["local_alsa_peer_t"]["alsa_port"] =
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
  for (const auto &r : rows) {
    if (r.type.value_or("") != "local_alsa_peer_t")
      continue;
    if (!r.alsa_subscribe_from)
      continue;
    if (r.alsa_subscribe_from->client == client &&
        r.alsa_subscribe_from->port == port) {
      return static_cast<peer_id_t>(r.id.value_or(0));
    }
  }
  return std::nullopt;
}

static std::optional<peer_id_t>
find_peer_for_raw(const std::vector<router_peer_row_t> &rows,
                  const std::string &device) {
  for (const auto &r : rows) {
    if (r.type.value_or("") != "local_rawmidi_peer_t")
      continue;
    if (r.device && *r.device == device)
      return static_cast<peer_id_t>(r.id.value_or(0));
  }
  return std::nullopt;
}

static std::optional<peer_id_t>
find_peer_for_host(const std::vector<router_peer_row_t> &rows,
                   const std::string &hostname, const std::string &port) {
  for (const auto &r : rows) {
    if (r.type.value_or("") != "network_rtpmidi_client_t")
      continue;
    const auto rh = r.connect_hostname.value_or("");
    const auto rp = r.connect_port.value_or("");
    if (rh == hostname && rp == port)
      return static_cast<peer_id_t>(r.id.value_or(0));
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
    auto peer = make_local_alsa_peer(name, ctx.aseq, eid.client, eid.port);
    return ctx.router->add_peer(peer);
  }
  if (eid.kind == endpoint_kind_e::RAW) {
    const auto found = find_peer_for_raw(rows, eid.device);
    if (found && *found != 0)
      return *found;
    const std::string name = FMT::format("WEB:RAW:{}", eid.device);
    auto peer = make_rawmidi_peer(name, eid.device);
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
    auto peer = make_network_rtpmidi_client(nm, eid.hostname, eid.hostport);
    return ctx.router->add_peer(peer);
  }
  throw std::runtime_error("Unknown endpoint kind");
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
        return respond_ok(env);
      }

      const auto rows = router_rows_snapshot(ctx);
      const auto pa = ensure_peer_for_endpoint(ctx, a, rows);
      const auto pb = ensure_peer_for_endpoint(ctx, b, rows);
      ctx.router->enqueue_connect(pa, pb);
      if (bidi)
        ctx.router->enqueue_connect(pb, pa);
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
      ctx.router->add_peer(make_local_alsa_listener(
          ctx.router, p.name.value_or(p.hostname), p.hostname,
          p.port.value_or("5004"), ctx.aseq, "0"));
      return respond_ok(env);
    }
    if (env.method == "router.create.local_rawmidi") {
      auto p = parse_rpc_params<create_local_rawmidi_params_t>(params);
      auto peer = make_rawmidi_peer(p.name, p.device);
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_client") {
      auto p = parse_rpc_params<create_network_rtpmidi_client_params_t>(params);
      auto peer = make_network_rtpmidi_client(p.name, p.hostname, p.port);
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_listener") {
      auto p = parse_rpc_params<create_network_rtpmidi_listener_params_t>(params);
      auto peer = make_network_rtpmidi_listener(p.name, std::to_string(p.udp_port));
      ctx.router->add_peer(peer);
      return respond(env, peer->status());
    }
    if (env.method == "router.create.local_alsa_peer") {
      auto p = parse_rpc_params<create_local_alsa_peer_params_t>(params);
      auto peer = (p.alsa_client && p.alsa_port)
                      ? make_local_alsa_peer(p.name, ctx.aseq, *p.alsa_client, *p.alsa_port)
                      : make_local_alsa_peer(p.name, ctx.aseq);
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
