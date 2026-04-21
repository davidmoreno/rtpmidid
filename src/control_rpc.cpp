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

static void write_rpc_error(::rtpmididns::dmjson::writer_t &w,
                            const dmjson::rpc::envelope_info_t &env,
                            std::string_view msg) {
  w.begin_object();
  w.key("id");
  w.raw(env.has_id ? env.id_json : std::string("null"));
  w.key("error");
  w.string_value(msg);
  w.end_object();
}

static void write_rpc_result_json(::rtpmididns::dmjson::writer_t &w,
                                  const dmjson::rpc::envelope_info_t &env,
                                  std::string_view result_json) {
  w.begin_object();
  w.key("id");
  w.raw(env.has_id ? env.id_json : std::string("null"));
  w.key("result");
  w.raw(result_json);
  w.end_object();
}

// Finish a response by serialising a typed value via the generated to_json.
template <typename T>
static std::string respond(dmjson::writer_t &w,
                           const dmjson::rpc::envelope_info_t &env,
                           const T &value) {
  write_rpc_result_json(w, env, dmjson::to_json(value));
  std::string out;
  w.swap_into_string(out);
  return out + "\n";
}

// Finish a response with a vector of typed values.
template <typename Row>
static std::string respond_rows(dmjson::writer_t &w,
                                const dmjson::rpc::envelope_info_t &env,
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
  write_rpc_result_json(w, env, arr_json);
  std::string out;
  w.swap_into_string(out);
  return out + "\n";
}

// Finish a response with `["ok"]`.
static std::string respond_ok(dmjson::writer_t &w,
                              const dmjson::rpc::envelope_info_t &env) {
  write_rpc_result_json(w, env, R"(["ok"])");
  std::string out;
  w.swap_into_string(out);
  return out + "\n";
}

// Finish an error response.
static std::string respond_error(dmjson::writer_t &w,
                                 const dmjson::rpc::envelope_info_t &env,
                                 std::string_view msg) {
  write_rpc_error(w, env, msg);
  std::string out;
  w.swap_into_string(out);
  return out + "\n";
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
      {"connect", "Connect to a remote RTP-MIDI server (object {hostname, port?, name?})"},
      {"router.create.local_rawmidi", "Create raw MIDI peer"},
      {"router.create.network_rtpmidi_client", "Create RTP-MIDI client peer"},
      {"router.create.network_rtpmidi_listener", "Create RTP-MIDI listener peer"},
      {"router.create.local_alsa_peer", "Create ALSA peer"},
      {"router.create.list", "List create peer schemas"},
      {"mdns.remove", "Delete an mDNS announcement"},
      {"export.rawmidi", "Export a rawmidi device to RTP"},
      {"midi.listAlsaSeq", "List ALSA sequencer ports"},
      {"midi.listRawMidi", "List raw MIDI devices"},
      {"help", "Return help text"},
  };
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

  dmjson::writer_t w;

  try {
    if (env.method == "status") {
      daemon_status_t ds;
      ds.version = VERSION;
      ds.settings.alsa_name = settings.alsa_name;
      ds.settings.control_filename = settings.control_filename;
      ds.router = ctx.router->status_rows();
      ds.mdns = mdns_snapshot(ctx.mdns);
      return respond(w, env, ds);
    }
    if (env.method == "router.remove") {
      auto p = parse_rpc_params<router_remove_params_t>(params);
      ctx.router->enqueue_remove_peer(static_cast<peer_id_t>(p.peer_id));
      return respond_ok(w, env);
    }
    if (env.method == "router.connect") {
      auto p = parse_rpc_params<router_connect_params_t>(params);
      ctx.router->enqueue_connect(static_cast<peer_id_t>(p.from),
                                  static_cast<peer_id_t>(p.to));
      return respond_ok(w, env);
    }
    if (env.method == "router.disconnect") {
      auto p = parse_rpc_params<router_connect_params_t>(params);
      ctx.router->enqueue_disconnect(static_cast<peer_id_t>(p.from),
                                     static_cast<peer_id_t>(p.to));
      return respond_ok(w, env);
    }
    if (env.method == "connect") {
      auto p = parse_rpc_params<connect_params_t>(params);
      if (p.hostname.empty())
        throw std::runtime_error("Need object {hostname, port?, name?}");
      ctx.router->add_peer(make_local_alsa_listener(
          ctx.router, p.name.value_or(p.hostname), p.hostname,
          p.port.value_or("5004"), ctx.aseq, "0"));
      return respond_ok(w, env);
    }
    if (env.method == "router.create.local_rawmidi") {
      auto p = parse_rpc_params<create_local_rawmidi_params_t>(params);
      auto peer = make_rawmidi_peer(p.name, p.device);
      ctx.router->add_peer(peer);
      return respond(w, env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_client") {
      auto p = parse_rpc_params<create_network_rtpmidi_client_params_t>(params);
      auto peer = make_network_rtpmidi_client(p.name, p.hostname, p.port);
      ctx.router->add_peer(peer);
      return respond(w, env, peer->status());
    }
    if (env.method == "router.create.network_rtpmidi_listener") {
      auto p = parse_rpc_params<create_network_rtpmidi_listener_params_t>(params);
      auto peer = make_network_rtpmidi_listener(p.name, std::to_string(p.udp_port));
      ctx.router->add_peer(peer);
      return respond(w, env, peer->status());
    }
    if (env.method == "router.create.local_alsa_peer") {
      auto p = parse_rpc_params<create_local_alsa_peer_params_t>(params);
      auto peer = (p.alsa_client && p.alsa_port)
                      ? make_local_alsa_peer(p.name, ctx.aseq, *p.alsa_client, *p.alsa_port)
                      : make_local_alsa_peer(p.name, ctx.aseq);
      ctx.router->add_peer(peer);
      return respond(w, env, peer->status());
    }
    if (env.method == "router.create.list")
      return respond(w, env, build_router_create_list());
    if (env.method == "mdns.remove") {
      if (!ctx.mdns)
        throw std::runtime_error("mDNS not available");
      auto p = parse_rpc_params<mdns_remove_params_t>(params);
      ctx.mdns->remove_announcement(p.name, p.hostname.value_or(""), p.port);
      return respond_ok(w, env);
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
        return respond(w, env, err);
      }
      rtpmididns::settings_t::rawmidi_t rawmidi;
      rawmidi.device = p.device;
      rawmidi.name = p.name.value_or("");
      rawmidi.local_udp_port = p.local_udp_port.value_or("0");
      rawmidi.remote_udp_port = p.remote_udp_port.value_or("0");
      rawmidi.hostname = p.hostname.value_or("");
      create_rawmidi_rtpclient_pair(ctx.router.get(), rawmidi);
      return respond_ok(w, env);
    }
    if (env.method == "midi.listAlsaSeq") {
      if (!ctx.aseq)
        return respond(w, env, rpc_error_body_t{"ALSA sequencer not available"});
      return respond_rows(w, env, ctx.aseq->enumerate_exported_ports());
    }
    if (env.method == "midi.listRawMidi")
      return respond_rows(w, env, enumerate_rawmidi_devices());
    if (env.method == "help")
      return respond_rows(w, env, build_help_entries());

    std::smatch match;
    if (std::regex_match(env.method, match, PEER_COMMAND_RE)) {
      const auto peer_id = static_cast<peer_id_t>(std::stoul(match[1].str()));
      auto peer = ctx.router->get_peer_by_id(peer_id);
      if (!peer)
        return respond_error(w, env, FMT::format("Unknown peer '{}'", peer_id));
      std::string perr;
      dmjson::writer_t inner;
      if (!peer->control_peer_command(match[2].str(), params, inner, perr))
        return respond_error(w, env, perr);
      write_rpc_result_json(w, env, inner.view());
      std::string out;
      w.swap_into_string(out);
      return out + "\n";
    }

    return respond_error(w, env, FMT::format("Unknown method '{}'", env.method));
  } catch (const std::exception &e) {
    return respond_error(w, env, e.what());
  }
}

} // namespace rtpmididns
