/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2025 David Moreno Montero <dmoreno@coralbits.com>
 */
#include "control_rpc.hpp"
#include "factory.hpp"
#include "midirouter.hpp"
#include <rtpmidid/logger.hpp>
#include "midipeer.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <regex>
#include <rtpmidid/mdns_rtpmidi.hpp>

namespace rtpmididns {
extern const char *VERSION;
}

namespace rtpmididns {

static const std::regex PEER_COMMAND_RE = std::regex("^(\\d*)\\.(.*)");

static std::string maybe_string(const json_t &j, const char *key,
                                const char *def) {
  if (j.is_object()) {
    auto it = j.find(key);
    if (it != j.end()) {
      if (it->is_string()) {
        return it->get<std::string>();
      }
      if (it->is_number_integer()) {
        return std::to_string(it->get<int64_t>());
      }
      if (it->is_number_unsigned()) {
        return std::to_string(it->get<uint64_t>());
      }
      if (it->is_number_float()) {
        return std::to_string(it->get<double>());
      }
    }
  }
  return def;
}

json_t mdns_status(const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns) {
  if (!mdns)
    return json_t{"status", "Not available"};

  std::vector<json_t> announcements;
  for (auto &announcement : mdns->announcements) {
    announcements.push_back({
        {"name", announcement.name},
        {"port", announcement.port},
    });
  }

  std::vector<json_t> remote_announcements;
  for (auto &announcement : mdns->remote_announcements) {
    remote_announcements.push_back({
        {"name", announcement.name},
        {"hostname", announcement.address},
        {"ip", announcement.ip},
        {"port", announcement.port},
    });
  }

  return json_t{
      {"status", "Available"},
      {"announcements", announcements},
      {"remote_announcements", remote_announcements},
  };
}

namespace control_rpc_ns {
struct command_t {
  const char *name;
  const char *description;
  std::function<json_t(control_rpc_context_t &, const json_t &)> func;
};
} // namespace control_rpc_ns

// NOLINTNEXTLINE
static const std::vector<control_rpc_ns::command_t> COMMANDS{
    {"status", "Return status of the daemon",
     [](control_rpc_context_t &ctx, const json_t &) {
       return json_t{
           {"version", rtpmididns::VERSION},
           {"settings",
            {
                {"alsa_name", rtpmididns::settings.alsa_name},
                {"control_filename", rtpmididns::settings.control_filename},
            }},
           {"router", ctx.router->status()},
           {"mdns", mdns_status(ctx.mdns)},
       };
     }},
    {"router.remove", "Remove a peer from the router",
     [](control_rpc_context_t &ctx, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t peer_id = params[0];
       DEBUG("Remove peer_id {}", peer_id);
       ctx.router->enqueue_remove_peer(peer_id);
       return json_t{"ok"};
     }},
    {"router.connect",
     "Connects two peers at the router. Unidirectional connection.",
     [](control_rpc_context_t &ctx, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t from_peer_id = params["from"];
       peer_id_t to_peer_id = params["to"];
       DEBUG("Connect peers: {} -> {}", from_peer_id, to_peer_id);
       ctx.router->enqueue_connect(from_peer_id, to_peer_id);
       return json_t{"ok"};
     }},
    {"router.disconnect",
     "Disconnects two peers at the router. Unidirectional connection.",
     [](control_rpc_context_t &ctx, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t from_peer_id = params["from"];
       peer_id_t to_peer_id = params["to"];
       DEBUG("Disconnect peers: {} -> {}", from_peer_id, to_peer_id);
       ctx.router->enqueue_disconnect(from_peer_id, to_peer_id);
       return json_t{"ok"};
     }},
    {"connect",
     "Connect to a peer send params: [hostname] | [hostname, port] | [name, "
     "hostname, port] | {\"name\": name, \"hostname\": hostname, \"port\": "
     "port}",
     [](control_rpc_context_t &ctx, const json_t &params) {
       std::string name, hostname, port;
       bool error = false;
       if (params.is_array()) {
         switch (params.size()) {
         case 1:
           name = hostname = params[0];
           port = "5004";
           break;
         case 2:
           name = hostname = params[0];
           port = params[1];
           break;
         case 3:
           name = params[0];
           hostname = params[1];
           port = to_string(params[2]);
           break;
         default:
           error = true;
         }
       } else if (params.is_object()) {
         name = params["name"];
         hostname = params["hostname"];
         port = to_string(params["port"]);

         if (name.empty() || hostname.empty() || port.empty()) {
           error = true;
         }
       } else {
         error = true;
       }
       if (error)
         return json_t{"error",
                       "Need 1 param (hostname:hostname:5004), 2 params "
                       "(hostname:port), "
                       "3 params (name,hostname,port) or a dict{name, "
                       "hostname, port}"};

       ctx.router->add_peer(make_local_alsa_listener(
           ctx.router, name, hostname, port, ctx.aseq, "0"));
       return json_t{"ok"};
     }},
    {"router.create", "Create a new peer of the specific type and params",
     [](control_rpc_context_t &ctx, const json_t &params) {
       DEBUG("Create peer: {}", params.dump());
       std::string type = params["type"];
       if (type == "local_rawmidi_t") {
         auto peer = make_rawmidi_peer(params["name"], params["device"]);
         ctx.router->add_peer(peer);
         return peer->status();
       }
       if (type == "network_rtpmidi_client_t") {
         auto peer = make_network_rtpmidi_client(
             params["name"], params["hostname"], to_string(params["port"]));
         ctx.router->add_peer(peer);
         return peer->status();
       }
       if (type == "network_rtpmidi_listener_t") {
         auto peer =
             make_network_rtpmidi_listener(params["name"], params["udp_port"]);
         ctx.router->add_peer(peer);
         return peer->status();
       }
       if (type == "local_alsa_peer_t") {
         auto peer = make_local_alsa_peer(params["name"], ctx.aseq);
         ctx.router->add_peer(peer);
         return peer->status();
       }
       if (type == "list") {
         return json_t{
             {"local_rawmidi_t",
              {{"name", "Name of the peer"}, {"device", "Path to the device"}}},
             {"network_rtpmidi_client_t",
              {{"name", "Name of the peer"},
               {"hostname", "Hostname of the server"},
               {"port", "Port of the server"}}},
             {"network_rtpmidi_listener_t",
              {{"name", "Name of the peer"},
               {"udp_port", "UDP port to listen [random]"}}},
             {"local_alsa_peer_t", {{"name", "Name of the peer"}}},
         };
       }
       ERROR("Unknown peer type or non construtible yet: {}", type);
       return json_t{{"error", "Unknown peer type"}};
     }},
    {"mdns.remove", "Delete a mdns announcement",
     [](control_rpc_context_t &ctx, const json_t &params) {
       if (!ctx.mdns) {
         return json_t{{"error", "mDNS not available"}};
       }
       DEBUG("Params {}", params.dump());
       std::string name = params["name"];
       std::string hostname;
       if (!params["hostname"].is_null()) {
         hostname = params["hostname"];
       }
       int32_t port = params["port"];
       DEBUG("Delete mdns announcement {}", name);
       ctx.mdns->remove_announcement(name, hostname, port);
       return json_t{"ok"};
     }},
    {"export.rawmidi", "Exports a rawmidi device to ALSA",
     [](control_rpc_context_t &ctx, const json_t &params) {
       rtpmididns::settings_t::rawmidi_t rawmidi;
       DEBUG("Export rawmidi: {}", params.dump());

       if (!params.is_object() || params["device"].is_null()) {
         json_t p;
         p["device"] = "Path to the device. Mandatory.";
         p["name"] = "Name of the peer";
         p["local_udp_port"] = "Local UDP port";
         p["remote_udp_port"] = "Remote UDP port";
         p["hostname"] =
             "Hostname of the server if want to connect to. Else is a local "
             "listener.";
         return json_t{{{"error", "Need device"}, {"params", p}}};
       }

       rawmidi.device = params["device"];
       rawmidi.name = maybe_string(params, "name", "");
       rawmidi.local_udp_port = maybe_string(params, "local_udp_port", "0");
       rawmidi.remote_udp_port = maybe_string(params, "remote_udp_port", "0");
       rawmidi.hostname = maybe_string(params, "hostname", "");
       create_rawmidi_rtpclient_pair(ctx.router.get(), rawmidi);
       return json_t{"ok"};
     }},
    {"help", "Return help text",
     [](control_rpc_context_t &, const json_t &) {
       auto res = std::vector<json_t>{};
       for (const auto &cmd : COMMANDS) {
         res.push_back({{"name", cmd.name}, {"description", cmd.description}});
       }
       return res;
     }},
};

json_t control_rpc_dispatch(control_rpc_context_t &ctx, const json_t &js) {
  json_t retdata = json_t::object();
  retdata["id"] = js.contains("id") ? js.at("id") : json_t(nullptr);
  try {
    if (!js.contains("method") || !js.at("method").is_string()) {
      retdata["error"] = "Missing method";
      return retdata;
    }
    const std::string method = js.at("method").get<std::string>();
    const json_t &params = js.contains("params") ? js.at("params") : json_t();

    for (const auto &cmd : COMMANDS) {
      if (cmd.name == method) {
        auto res = cmd.func(ctx, params);
        retdata["result"] = std::move(res);
        return retdata;
      }
    }

    std::smatch match;
    if (std::regex_match(method, match, PEER_COMMAND_RE)) {
      const auto peer_id = static_cast<peer_id_t>(std::stoi(match[1].str()));
      const std::string pcmd = match[2].str();
      auto peer = ctx.router->get_peer_by_id(peer_id);
      if (peer) {
        auto res = peer->command(pcmd, params);
        if (res.contains("error")) {
          retdata["error"] = res["error"];
        } else {
          retdata["result"] = std::move(res);
        }
      } else {
        retdata["error"] = FMT::format("Unknown peer '{}'", peer_id);
      }
      return retdata;
    }

    retdata["error"] = FMT::format("Unknown method '{}'", method);
    ERROR("Error running method: {}", std::string(retdata["error"]));
    return retdata;
  } catch (const std::exception &e) {
    ERROR("Error running method: {}", e.what());
    retdata["error"] = e.what();
    return retdata;
  }
}

} // namespace rtpmididns
