/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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
#include "control_socket.hpp"
#include "factory.hpp"
#include "settings.hpp"
#include <sys/stat.h>
#include <sys/un.h>

#include "json.hpp"
#include "midipeer.hpp"
#include "stringpp.hpp"
#include <rtpmidid/mdns_rtpmidi.hpp>

namespace rtpmididns {
// NOLINTNEXTLINE
extern const char *VERSION;

const char *const MSG_CLOSE_CONN =
    "{\"event\": \"close\", \"detail\": \"Shutdown\", \"code\": 0}\n";
const char *const MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";
// const char *const MSG_UNKNOWN_COMMAND =
//     "{\"error\": \"Unknown command\", \"code\": 2}";

static const std::regex PEER_COMMAND_RE = std::regex("^(\\d*)\\.(.*)");

control_socket_t::control_socket_t() {
  std::string &socketfile = settings.control_filename;

  int ret = unlink(socketfile.c_str());
  if (ret >= 0) {
    INFO("Removed old control socket. Creating new one.");
  }

  socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket == -1) {
    ERROR("Error creating socket: {}", strerror(errno));
    return;
  }
  struct sockaddr_un addr = {};
  // memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  // NOLINTNEXTLINE
  strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);

  // NOLINTNEXTLINE
  ret = bind(socket, (const struct sockaddr *)(&addr),
             sizeof(struct sockaddr_un));
  if (ret == -1) {
    ERROR("Error Binding socket at {}: {}", socketfile, strerror(errno));
    close(socket);
    socket = -1;
    return;
  }
  if (listen(socket, 20) == -1) {
    ERROR("Error Listening to socket at {}: {}", socketfile, strerror(errno));
    close(socket);
    socket = -1;
    return;
  }
  ::chmod(socketfile.c_str(), 0777);
  connection_listener = rtpmidid::poller.add_fd_in(
      socket, [this](int fd) { this->connection_ready(); });
  INFO("Control socket ready at {}", socketfile);
  start_time = time(NULL);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
rtpmididns::control_socket_t::~control_socket_t() noexcept {
  for (auto &client : clients) {
    client.listener.stop();

    auto n = write(client.fd, MSG_CLOSE_CONN, strlen(MSG_CLOSE_CONN));
    if (n < 0) {
      DEBUG("Could not send goodbye packet to control.");
    }
    close(client.fd);
  }
  connection_listener.stop();
  close(socket);
  DEBUG("Closed control socket");
}

void rtpmididns::control_socket_t::connection_ready() {
  int fd = accept(socket, NULL, NULL);

  if (fd != -1) {
    client_t client;
    client.listener = rtpmidid::poller.add_fd_in(
        fd, [this](int fd) { this->data_ready(fd); });
    client.fd = fd;
    clients.push_back(std::move(client));
    // DEBUG("Added control connection: {}", fd);
  } else {
    ERROR("\"accept()\" failed, continuing...");
  }
}

void control_socket_t::data_ready(int fd) {
  char buf[1024];                           // NOLINT
  size_t l = recv(fd, buf, sizeof(buf), 0); // NOLINT
  if (l <= 0) {
    // DEBUG("Closed control connection: {}", fd);
    auto I = std::find_if(clients.begin(), clients.end(),
                          [fd](auto &client) { return client.fd == fd; });
    I->listener.stop();
    close(I->fd);
    clients.erase(I);
    return;
  }
  if (l >= sizeof(buf) - 1) {
    auto w = write(fd, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
    if (w < 0) {
      ERROR(
          "Could not send msg too long to control socket! Closing connection.");
      ::close(fd);
    }
    return;
  }
  buf[l] = 0;                               // NOLINT
  auto ret = parse_command(trim_copy(buf)); // NOLINT
  ret += "\n";
  auto w = write(fd, ret.c_str(), ret.length());
  if (w < 0) {
    ERROR("Could not send msg to control socket! Closing Connection.");
    ::close(fd);
    fd = -1;
  }
  if (fd != -1)
    fsync(fd);
}

static std::string maybe_string(const json_t &j, const char *key,
                                const char *def) {
  if (j.is_object()) {
    auto it = j.find(key);
    if (it != j.end()) {
      return it->get<std::string>();
    }
  }
  return def;
};

namespace control_socket_ns {
struct command_t {
  const char *name;
  const char *description;
  std::function<json_t(rtpmididns::control_socket_t &, const json_t &)> func;
};
} // namespace control_socket_ns

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
        {"port", announcement.port},
    });
  }

  return json_t{
      {"status", "Available"},
      {"announcements", announcements},
      {"remote_announcements", remote_announcements},
  };
}

// NOLINTNEXTLINE
const std::vector<control_socket_ns::command_t> COMMANDS{
    {"status", "Return status of the daemon",
     [](control_socket_t &control, const json_t &) {
       return json_t{
           {"version", rtpmididns::VERSION},
           {"settings",
            {
                {"alsa_name", rtpmididns::settings.alsa_name},
                {"control_filename", rtpmididns::settings.control_filename} //
            }},
           {"router", control.router->status()}, //
           {"mdns", mdns_status(control.mdns)},  //
       };
     }},
    {"router.remove", "Remove a peer from the router",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t peer_id = params[0];
       DEBUG("Remove peer_id {}", peer_id);
       control.router->remove_peer(peer_id);
       return "ok";
     }},
    {"router.connect",
     "Connects two peers at the router. Unidirectional connection.",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t from_peer_id = params["from"];
       peer_id_t to_peer_id = params["to"];
       DEBUG("Connect peers: {} -> {}", from_peer_id, to_peer_id);
       control.router->connect(from_peer_id, to_peer_id);
       return "ok";
     }},
    {"router.disconnect",
     "Disconnects two peers at the router. Unidirectional connection.",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t from_peer_id = params["from"];
       peer_id_t to_peer_id = params["to"];
       DEBUG("Disconnect peers: {} -> {}", from_peer_id, to_peer_id);
       control.router->disconnect(from_peer_id, to_peer_id);
       return "ok";
     }},
    {"connect",
     "Connect to a peer send params: [hostname] | [hostname, port] | [name, "
     "hostname, port] | {\"name\": name, \"hostname\": hostname, \"port\": "
     "port}",
     [](control_socket_t &control, const json_t &params) {
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

       control.router->add_peer(make_local_alsa_listener(
           control.router, name, hostname, port, control.aseq, "0"));
       return json_t{"ok"};
     }},
    {"router.create", "Create a new peer of the specific type and params",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Create peer: {}", params.dump());
       std::string type = params["type"];
       if (type == "local_rawmidi_t") {
         auto peer = make_rawmidi_peer(params["name"], params["device"]);
         control.router->add_peer(peer);
         return peer->status();
       } else if (type == "network_rtpmidi_client_t") {
         auto peer = make_network_rtpmidi_client(
             params["name"], params["hostname"], to_string(params["port"]));
         control.router->add_peer(peer);
         return peer->status();
       } else if (type == "network_rtpmidi_listener_t") {
         auto peer =
             make_network_rtpmidi_listener(params["name"], params["udp_port"]);
         control.router->add_peer(peer);
         return peer->status();
       } else if (type == "local_alsa_peer_t") {
         auto peer = make_local_alsa_peer(params["name"], control.aseq);
         control.router->add_peer(peer);
         return peer->status();
       } else if (type == "list") {
         return json_t{
             {"local_rawmidi_t",
              {{"name", "Name of the peer"}, {"device", "Path to the device"}}},
             //
             {"network_rtpmidi_client_t",
              {{"name", "Name of the peer"},
               {"hostname", "Hostname of the server"},
               {"port", "Port of the server"}}},
             //
             {"network_rtpmidi_listener_t",
              {{"name", "Name of the peer"},
               {"udp_port", "UDP port to listen [random]"}}},
             //
             {"local_alsa_peer_t", {{"name", "Name of the peer"}}}
             //
         };

       } else {
         ERROR("Unknown peer type or non construtible yet: {}", type);
         return json_t{{"error", "Unknown peer type"}};
       }
     }},
    {"mdns.remove", "Delete a mdns announcement",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Params {}", params.dump());
       std::string name = params["name"];
       std::string hostname;
       if (!params["hostname"].is_null()) {
         hostname = params["hostname"];
       }
       int32_t port = params["port"];
       DEBUG("Delete mdns announcement {}", name);
       control.mdns->remove_announcement(name, hostname, port);
       return "ok";
     }},
    {"export.rawmidi", "Exports a rawmidi device to ALSA",
     [](control_socket_t &control, const json_t &params) {
       // Just set the data into settings_t::rawmidi_t
       rtpmididns::settings_t::rawmidi_t rawmidi;
       DEBUG("Export rawmidi: {}", params.dump());

       // if not device, return help with params
       if (!params.is_object() || params["device"].is_null()) {
         return json_t{{
             //
             {"error", "Need device"},
             {"params",
              {{"device", "Path to the device. Mandatory."},
               {"name", "Name of the peer"},
               {"local_udp_port", "Local UDP port"},
               {"remote_udp_port", "Remote UDP port"},
               {"hostname", "Hostname of the server if want to connect to. "
                            "Else is a local listener."}}}
             //
         }};
       }

       rawmidi.device = params["device"];
       rawmidi.name = maybe_string(params, "name", "");
       rawmidi.local_udp_port = maybe_string(params, "local_udp_port", "0");
       rawmidi.remote_udp_port = maybe_string(params, "remote_udp_port", "0");
       rawmidi.hostname = maybe_string(params, "hostname", "");
       create_rawmidi_rtpclient_pair(control.router.get(), rawmidi);
       return json_t{"ok"};
     }},
    // Return some help text
    {"help", "Return help text",
     [](control_socket_t &control, const json_t &) {
       auto res = std::vector<json_t>{};
       for (const auto &cmd : COMMANDS) {
         res.push_back({{"name", cmd.name}, {"description", cmd.description}});
       }
       return res;
     }},
    //
};

std::string control_socket_t::parse_command(const std::string &command) {
  // DEBUG("Parse command {}", command);
  auto js = json_t::parse(command);

  std::string method = js["method"];
  json_t retdata = {{"id", js["id"]}};
  try {
    for (const auto &cmd : COMMANDS) {
      if (cmd.name == method) {
        auto res = cmd.func(*this, js["params"]);
        retdata["result"] = res;

        return retdata.dump();
      }
    }
    // if matches the regex (^d*\..*), its a command to a peer
    std::smatch match;
    if (std::regex_match(method, match, PEER_COMMAND_RE)) {
      auto peer_id = std::stoi(match[1]);
      auto cmd = match[2];
      // DEBUG("Peer command: {} -> {}", peer_id, cmd.to_string());
      auto peer = router->get_peer_by_id(peer_id);
      if (peer) {
        auto res = peer->command(cmd, js["params"]);
        if (res.contains("error")) {
          retdata["error"] = res["error"];
        } else {
          retdata["result"] = res;
        }
      } else {
        retdata["error"] = FMT::format("Unknown peer '{}'", peer_id);
      }
      return retdata.dump();
    }

    retdata["error"] = FMT::format("Unknown method '{}'", method);
    ERROR("Error running method: {}", std::string(retdata["error"]));
    return retdata.dump();
  } catch (const std::exception &e) {
    ERROR("Error running method: {}", e.what());
    retdata["error"] = e.what();
    return retdata.dump();
  }
}
} // namespace rtpmididns
