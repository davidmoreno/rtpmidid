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
#include "peer_spawn.hpp"
#include "peer_factory.hpp"
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
  o.schemas["identity"]["format"] =
      "type_prefix:key=value,... (e.g. rawmidi:device=/dev/snd/midiC0D0)";
  return o;
}

static std::vector<rpc_help_entry_t> build_help_entries() {
  return {
      {"status", "Return status of the daemon"},
      {"router.remove", "Remove a peer from the router"},
      {"router.connect", "Connects two peers at the router. Unidirectional connection."},
      {"router.disconnect", "Disconnects two peers at the router."},
      {"router.create", "Create a peer from a device identity string"},
      {"endpoint.connect",
       "Connect two device identities (router peers or pure ALSA aconnect)"},
      {"endpoint.disconnect", "Disconnect two device identities"},
      {"connect", "Connect to a remote RTP-MIDI server (object {hostname, port?, name?})"},
      {"router.create.list", "List create peer identity format"},
      {"mdns.remove", "Delete an mDNS announcement"},
      {"midi.listAlsaSeq", "List ALSA sequencer ports"},
      {"midi.listAlsaSubscriptions", "List ALSA sequencer subscriptions (aconnect links)"},
      {"midi.listRawMidi", "List raw MIDI devices"},
      {"monitor.start",
       "Start Web UI MIDI monitor for a target device identity"},
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

static peer_factory_context_t factory_context(const control_rpc_context_t &ctx) {
  return make_factory_context(ctx.aseq, ctx.router, ctx.mdns);
}

static peer_id_t ensure_peer_for_side(control_rpc_context_t &ctx,
                                      std::string_view side) {
  if (!ctx.router)
    throw std::runtime_error("router not available");
  return ensure_peer_for_identity(factory_context(ctx), ctx.router, side);
}

/** Match an online router peer only — never create peers (used for disconnect). */
static std::optional<peer_id_t>
peer_id_for_disconnect_side(control_rpc_context_t &ctx, std::string_view side) {
  if (!ctx.router)
    return std::nullopt;
  const auto online = collect_online_devices_from_router(ctx.router);
  const auto indices = match_side_to_devices(std::string(side), online);
  if (indices.empty())
    return std::nullopt;
  return online[indices.front()].peer_id;
}

static std::optional<aseq_t::port_t>
alsa_port_from_identity(const std::shared_ptr<aseq_t> &aseq,
                        const std::string &identity) {
  const auto id = device_identity_t::parse(identity);
  if (!id || id->type_prefix != "alsa_seq" || !aseq)
    return std::nullopt;
  const auto client = id->find("client");
  const auto port = id->find("port");
  if (!client || !port)
    return std::nullopt;
  for (const auto &row : aseq->enumerate_exported_ports()) {
    if (row.client_name == *client && row.port_name == *port)
      return aseq_t::port_t{static_cast<uint8_t>(row.client),
                            static_cast<uint8_t>(row.port)};
  }
  return std::nullopt;
}

static std::vector<router_peer_row_t>
router_rows_snapshot(const control_rpc_context_t &ctx) {
  return ctx.router ? ctx.router->status_rows()
                    : std::vector<router_peer_row_t>{};
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

static std::vector<online_device_t>
online_devices_snapshot(const control_rpc_context_t &ctx) {
  return collect_online_devices_from_router(ctx.router);
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
resolve_side_to_connection_side(const std::string &side) {
  if (device_query_t::parse(side) || device_identity_t::parse(side))
    return side;
  throw std::runtime_error(
      FMT::format("Invalid device identity or query for side '{}'", side));
}

static void unpersist_endpoint_pair(control_rpc_context_t &ctx,
                                    const std::string &from,
                                    const std::string &to) {
  if (!ctx.connection_db)
    return;
  try {
    const auto sa = resolve_side_to_connection_side(from);
    const auto sb = resolve_side_to_connection_side(to);
    if (sa && sb)
      ctx.connection_db->remove_stable_pair(*sa, *sb);
  } catch (const std::exception &e) {
    DEBUG("endpoint.disconnect: skip unpersist ({} -> {}): {}", from, to,
          e.what());
  }
}

static void maybe_persist_direct_endpoint_connection(
    control_rpc_context_t &ctx, const std::string &from_endpoint,
    const std::string &to_endpoint, bool bidi,
    const std::vector<router_peer_row_t> &rows) {
  if (!ctx.connection_db)
    return;
  const auto sa = resolve_side_to_connection_side(from_endpoint);
  const auto sb = resolve_side_to_connection_side(to_endpoint);
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
      if (p.from.empty() || p.to.empty())
        throw std::runtime_error("Need {from,to} device identity strings");
      const bool bidi = p.bidi.value_or(true);

      if (is_direct_alsa_side(p.from) && is_direct_alsa_side(p.to)) {
        if (!ctx.aseq)
          throw std::runtime_error("ALSA sequencer not available");
        const auto from_port = alsa_port_from_identity(ctx.aseq, p.from);
        const auto to_port = alsa_port_from_identity(ctx.aseq, p.to);
        if (!from_port || !to_port)
          throw std::runtime_error("Could not resolve ALSA ports from identity");
        ctx.aseq->connect_external(*from_port, *to_port);
        if (bidi)
          ctx.aseq->connect_external(*to_port, *from_port);
        const auto rows = router_rows_snapshot(ctx);
        maybe_persist_direct_endpoint_connection(ctx, p.from, p.to, bidi, rows);
        return respond_ok(env);
      }

      const auto pa = ensure_peer_for_side(ctx, p.from);
      const auto pb = ensure_peer_for_side(ctx, p.to);
      ctx.router->connect_blocking(pa, pb);
      if (bidi)
        ctx.router->connect_blocking(pb, pa);
      return respond_ok(env);
    }
    if (env.method == "endpoint.disconnect") {
      auto p = parse_rpc_params<endpoint_disconnect_params_t>(params);
      if (p.from.empty() || p.to.empty())
        throw std::runtime_error("Need {from,to} device identity strings");

      if (is_direct_alsa_side(p.from) && is_direct_alsa_side(p.to)) {
        if (!ctx.aseq)
          throw std::runtime_error("ALSA sequencer not available");
        const auto from_port = alsa_port_from_identity(ctx.aseq, p.from);
        const auto to_port = alsa_port_from_identity(ctx.aseq, p.to);
        if (!from_port || !to_port)
          throw std::runtime_error("Could not resolve ALSA ports from identity");
        ctx.aseq->disconnect_external(*from_port, *to_port);
        ctx.aseq->disconnect_external(*to_port, *from_port);
        unpersist_endpoint_pair(ctx, p.from, p.to);
        return respond_ok(env);
      }

      const auto pa = peer_id_for_disconnect_side(ctx, p.from);
      const auto pb = peer_id_for_disconnect_side(ctx, p.to);
      if (!pa || !pb)
        throw std::runtime_error(
            "Could not resolve online peers for disconnect (check device "
            "identities match the Devices tab)");
      ctx.router->enqueue_disconnect(*pa, *pb);
      ctx.router->enqueue_disconnect(*pb, *pa);
      return respond_ok(env);
    }
    if (env.method == "connect") {
      auto p = parse_rpc_params<connect_params_t>(params);
      if (p.hostname.empty())
        throw std::runtime_error("Need object {hostname, port?, name?}");
      device_identity_t id;
      id.type_prefix = "alsa_listener";
      id.fields.push_back(
          device_identity_field_t{"service", p.name.value_or(p.hostname), false});
      id.fields.push_back(device_identity_field_t{"hostname", p.hostname, false});
      id.fields.push_back(
          device_identity_field_t{"port", p.port.value_or("5004"), false});
      std::string err;
      auto peer = create_peer({id}, factory_context(ctx), &err);
      if (!peer)
        throw std::runtime_error(err.empty() ? "connect failed" : err);
      ctx.router->add_peer(*peer);
      return respond_ok(env);
    }
    if (env.method == "router.create") {
      auto p = parse_rpc_params<router_create_params_t>(params);
      if (p.identity.empty())
        throw std::runtime_error("Need {identity}");
      std::string err;
      auto peer = create_peer_from_string(p.identity, factory_context(ctx), &err);
      if (!peer)
        throw std::runtime_error(err.empty() ? "create failed" : err);
      ctx.router->add_peer(*peer);
      return respond(env, (*peer)->status());
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
      if (p.identity.empty())
        throw std::runtime_error("Need {identity}");
      const peer_id_t target = ensure_peer_for_side(ctx, p.identity);
      const std::string uuid = random_uuid_v4();
      auto peer_base = make_webui_midi_monitor_peer(uuid, target);
      auto mon =
          std::dynamic_pointer_cast<webui_midi_monitor_peer_t>(peer_base);
      if (!mon)
        throw std::runtime_error("internal: monitor peer");
      const peer_id_t mid = ctx.router->add_peer(peer_base);
      monitor_registry_register(uuid, mon);
      tee_monitor_edges(ctx, target, mid, mon);
      if (const auto target_port = alsa_port_from_identity(ctx.aseq, p.identity)) {
        setup_alsa_monitor_taps(
            ctx.aseq, ctx.router, target_port->client, target_port->port, target,
            mid, uuid,
            [&](uint8_t client, uint8_t port) {
              device_identity_t id;
              if (!ctx.aseq)
                throw std::runtime_error("ALSA sequencer not available");
              const auto cn = ctx.aseq->get_client_name_by_id(client);
              const auto pn = ctx.aseq->get_port_name(client, port);
              if (cn.empty() || pn.empty())
                throw std::runtime_error("Could not resolve ALSA names");
              const auto sid = identity_from_alsa_names(cn, pn);
              if (!sid)
                throw std::runtime_error("Could not build ALSA identity");
              return ensure_peer_for_side(ctx, sid->serialize());
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
      const auto sa = resolve_side_to_connection_side(p.side_a);
      const auto sb = resolve_side_to_connection_side(p.side_b);
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
      const auto sa = resolve_side_to_connection_side(p.side_a);
      const auto sb = resolve_side_to_connection_side(p.side_b);
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
      const auto sa = resolve_side_to_connection_side(p.side_a);
      const auto sb = resolve_side_to_connection_side(p.side_b);
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
      const auto sa = resolve_side_to_connection_side(p.side_a);
      const auto sb = resolve_side_to_connection_side(p.side_b);
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
        row.source = device_source_to_wire(d.source);
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
