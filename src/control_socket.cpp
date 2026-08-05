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

#include "control_commands_jsondm.hpp"
#include "control_socket.hpp"
#include "control_status_jsondm.hpp"
#include "factory.hpp"
#include "midipeer.hpp"
#include "peer_status_jsondm.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <functional>
#include <regex>
#include <rtpmidid/jsondm.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <string_view>
#include <sys/stat.h>
#include <sys/un.h>

namespace rtpmididns {
// NOLINTNEXTLINE
extern const char *VERSION;

const char *const MSG_CLOSE_CONN =
    "{\"event\": \"close\", \"detail\": \"Shutdown\", \"code\": 0}\n";
const char *const MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";

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
  auto remove_client = [&](int client_fd) {
    auto it =
        std::find_if(clients.begin(), clients.end(), [client_fd](auto &client) {
          return client.fd == client_fd;
        });
    if (it != clients.end()) {
      it->listener.stop();
      close(it->fd);
      clients.erase(it);
    } else if (client_fd >= 0) {
      ::close(client_fd);
    }
  };
  if (l <= 0) {
    remove_client(fd);
    return;
  }
  if (l >= sizeof(buf) - 1) {
    auto w = write(fd, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
    if (w < 0) {
      ERROR(
          "Could not send msg too long to control socket! Closing connection.");
      remove_client(fd);
    }
    return;
  }
  buf[l] = 0;                               // NOLINT
  auto ret = parse_command(trim_copy(buf)); // NOLINT
  ret += "\n";
  auto w = write(fd, ret.c_str(), ret.length());
  if (w < 0) {
    ERROR("Could not send msg to control socket! Closing Connection.");
    remove_client(fd);
  }
}

static mdns_status_t
mdns_status(const std::shared_ptr<rtpmidid::mdns_rtpmidi_t> &mdns) {
  mdns_status_t s;
  if (!mdns) {
    s.status = "Not available";
    return s;
  }
  s.status = "Available";
  for (auto &announcement : mdns->announcements) {
    s.announcements.push_back({announcement.name, announcement.port});
  }
  for (auto &announcement : mdns->remote_announcements) {
    s.remote_announcements.push_back(
        {announcement.name, announcement.address, announcement.port});
  }
  return s;
}

// ---------------------------------------------------------------------------
// Typed command registry
// ---------------------------------------------------------------------------

struct command_entry_t {
  const char *name;
  const char *description;
  std::function<void(control_socket_t &, std::string_view params_json,
                     std::string &result_out)>
      run;
};

template <typename ParamsT, typename ResultT>
command_entry_t make_command(const char *name, const char *description,
                             ResultT (*func)(control_socket_t &,
                                             const ParamsT &)) {
  return {name, description,
          [func](control_socket_t &c, std::string_view params_json,
                 std::string &out) {
            ParamsT params;
            jsondm::deserialize(params_json, params);
            jsondm::serialize(func(c, params), out);
          }};
}
template <typename ResultT>
command_entry_t make_command_noparams(const char *name, const char *description,
                                      ResultT (*func)(control_socket_t &)) {
  return {name, description,
          [func](control_socket_t &c, std::string_view, std::string &out) {
            jsondm::serialize(func(c), out);
          }};
}
// Command whose params need hand-written parsing (legacy multi-form params).
command_entry_t make_raw_command(const char *name, const char *description,
                                 std::string (*func)(control_socket_t &,
                                                     std::string_view)) {
  return {name, description,
          [func](control_socket_t &c, std::string_view params_json,
                 std::string &out) { out = func(c, params_json); }};
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static std::string port_to_string(const std::variant<std::string, int> &p) {
  return std::visit(
      [](const auto &v) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int>) {
          return std::to_string(v);
        } else {
          return v;
        }
      },
      p);
}

static daemon_status_t cmd_status(control_socket_t &control) {
  daemon_status_t s;
  s.version = VERSION;
  s.settings.alsa_name = settings.alsa_name;
  s.settings.control_filename = settings.control_filename;
  s.router = control.router->status();
  s.mdns = mdns_status(control.mdns);
  return s;
}

static std::string cmd_router_remove(control_socket_t &control,
                                     const remove_params_t &params) {
  control.router->remove_peer(params.peer_id);
  return "ok";
}

static std::string cmd_router_connect(control_socket_t &control,
                                      const router_connect_params_t &params) {
  control.router->connect(params.from, params.to);
  return "ok";
}

static std::string
cmd_router_disconnect(control_socket_t &control,
                      const router_connect_params_t &params) {
  control.router->disconnect(params.from, params.to);
  return "ok";
}

static create_result_t cmd_router_create(control_socket_t &control,
                                         const create_params_t &params) {
  if (params.type == "local_rawmidi_t") {
    if (!params.name || !params.device)
      throw jsondm::exception(
          "router.create: local_rawmidi_t needs name and device");
    auto peer = make_rawmidi_peer(*params.name, *params.device);
    control.router->add_peer(peer);
    return peer->status();
  } else if (params.type == "network_rtpmidi_client_t") {
    if (!params.name || !params.hostname || !params.port)
      throw jsondm::exception("router.create: network_rtpmidi_client_t needs "
                              "name, hostname and port");
    auto peer = make_network_rtpmidi_client(*params.name, *params.hostname,
                                            port_to_string(*params.port));
    control.router->add_peer(peer);
    return peer->status();
  } else if (params.type == "network_rtpmidi_listener_t") {
    if (!params.name || !params.udp_port)
      throw jsondm::exception(
          "router.create: network_rtpmidi_listener_t needs name and udp_port");
    auto peer = make_network_rtpmidi_listener(*params.name, *params.udp_port);
    control.router->add_peer(peer);
    return peer->status();
  } else if (params.type == "local_alsa_peer_t") {
    if (!params.name)
      throw jsondm::exception("router.create: local_alsa_peer_t needs name");
    auto peer = make_local_alsa_peer(*params.name, control.aseq);
    control.router->add_peer(peer);
    return peer->status();
  } else if (params.type == "list") {
    router_create_help_t help;
    help.local_rawmidi_t =
        router_create_help_entry_t{"Name of the peer", "Path to the device",
                                   std::nullopt, std::nullopt, std::nullopt};
    help.network_rtpmidi_client_t = router_create_help_entry_t{
        "Name of the peer", std::nullopt, "Hostname of the server",
        "Port of the server", std::nullopt};
    help.network_rtpmidi_listener_t = router_create_help_entry_t{
        "Name of the peer", std::nullopt, std::nullopt, std::nullopt,
        "UDP port to listen [random]"};
    help.local_alsa_peer_t =
        router_create_help_entry_t{"Name of the peer", std::nullopt,
                                   std::nullopt, std::nullopt, std::nullopt};
    return help;
  } else {
    ERROR("Unknown peer type or non construtible yet: {}", params.type);
    return command_error_t{"Unknown peer type"};
  }
}

static std::string cmd_mdns_remove(control_socket_t &control,
                                   const mdns_remove_params_t &params) {
  std::string hostname = params.hostname.value_or("");
  control.mdns->remove_announcement(params.name, hostname, params.port);
  return "ok";
}

static export_result_t
cmd_export_rawmidi(control_socket_t &control,
                   const export_rawmidi_params_t &params) {
  if (!params.device || params.device->empty()) {
    return export_error_t{
        "Need device",
        export_help_t{"Path to the device. Mandatory.", "Name of the peer",
                      "Local UDP port", "Remote UDP port",
                      "Hostname of the server if want to connect to. Else is "
                      "a local listener."}};
  }
  rtpmididns::settings_t::rawmidi_t rawmidi;
  rawmidi.device = *params.device;
  rawmidi.name = params.name.value_or("");
  rawmidi.local_udp_port = params.local_udp_port.value_or("0");
  rawmidi.remote_udp_port = params.remote_udp_port.value_or("0");
  rawmidi.hostname = params.hostname.value_or("");
  create_rawmidi_rtpclient_pair(control.router.get(), rawmidi);
  return std::vector<std::string>{"ok"};
}

// NOLINTNEXTLINE
extern const std::vector<command_entry_t> COMMANDS;

static std::vector<command_help_t> cmd_help(control_socket_t &) {
  std::vector<command_help_t> res;
  for (const auto &cmd : COMMANDS) {
    res.push_back({cmd.name, cmd.description});
  }
  return res;
}

static std::string cmd_connect(control_socket_t &control,
                               std::string_view params_json) {
  // Legacy accepts: [hostname] | [hostname,port] | [name,hostname,port] |
  // {name,hostname,port}; name defaults to hostname, port defaults to 5004.
  std::string name, hostname, port;
  bool error = false;
  try {
    jsondm::Reader r(params_json);
    auto t = r.peek_type();
    if (t == jsondm::Reader::type::array) {
      std::vector<std::string> parts;
      r.arr_begin();
      while (r.next_elem()) {
        std::string s;
        jsondm::read(r, s);
        parts.push_back(s);
      }
      r.arr_end();
      switch (parts.size()) {
      case 1:
        name = hostname = parts[0];
        port = "5004";
        break;
      case 2:
        name = hostname = parts[0];
        port = parts[1];
        break;
      case 3:
        name = parts[0];
        hostname = parts[1];
        port = parts[2];
        break;
      default:
        error = true;
      }
    } else if (t == jsondm::Reader::type::object) {
      connect_params_t p;
      jsondm::deserialize(params_json, p);
      name = p.name;
      hostname = p.hostname;
      port = p.port;
      if (name.empty() || hostname.empty() || port.empty())
        error = true;
    } else {
      error = true;
    }
  } catch (const jsondm::exception &) {
    error = true;
  }
  if (error) {
    std::string out;
    jsondm::serialize(
        command_error_t{"Need 1 param (hostname:hostname:5004), 2 params "
                        "(hostname:port), 3 params (name,hostname,port) or "
                        "a dict{name, hostname, port}"},
        out);
    return out;
  }
  control.router->add_peer(make_local_alsa_listener(
      control.router, name, hostname, port, control.aseq, "0"));
  std::string out;
  jsondm::serialize(std::vector<std::string>{"ok"}, out);
  return out;
}

// NOLINTNEXTLINE
const std::vector<command_entry_t> COMMANDS{
    make_command_noparams("status", "Return status of the daemon", cmd_status),
    make_command("router.remove", "Remove a peer from the router",
                 cmd_router_remove),
    make_command("router.connect",
                 "Connects two peers at the router. Unidirectional connection.",
                 cmd_router_connect),
    make_command(
        "router.disconnect",
        "Disconnects two peers at the router. Unidirectional connection.",
        cmd_router_disconnect),
    make_raw_command("connect",
                     "Connect to a peer send params: [hostname] | "
                     "[hostname, port] | [name, hostname, port] | {\"name\": "
                     "name, \"hostname\": hostname, \"port\": port}",
                     cmd_connect),
    make_command("router.create",
                 "Create a new peer of the specific type and params",
                 cmd_router_create),
    make_command("mdns.remove", "Delete a mdns announcement", cmd_mdns_remove),
    make_raw_command(
        "export.rawmidi", "Exports a rawmidi device to ALSA",
        [](control_socket_t &c, std::string_view p) -> std::string {
          export_rawmidi_params_t params;
          try {
            jsondm::deserialize(p, params);
          } catch (const jsondm::exception &) {
            std::string out;
            jsondm::serialize(
                export_error_t{"Need device",
                               export_help_t{"Path to the device. "
                                             "Mandatory.",
                                             "Name of the peer",
                                             "Local UDP port",
                                             "Remote UDP port",
                                             "Hostname of the server if "
                                             "want to connect to. Else "
                                             "is a local listener."}},
                out);
            return out;
          }
          std::string out;
          jsondm::serialize(cmd_export_rawmidi(c, params), out);
          return out;
        }),
    make_command_noparams("help", "Return help text", cmd_help),
};

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

// Returns the error string if the serialized result is an error object.
static std::optional<std::string> extract_error(std::string_view res) {
  try {
    jsondm::Reader r(res);
    r.obj_begin();
    while (r.next_key()) {
      if (r.key() == "error") {
        std::string e;
        jsondm::read(r, e);
        return e;
      }
      r.skip_value();
    }
  } catch (const jsondm::exception &) {
  }
  return std::nullopt;
}

static std::string compose_response(const std::optional<std::string> &id,
                                    const char *kind,
                                    std::string_view payload) {
  std::string out;
  jsondm::Writer w(out);
  w.obj_begin();
  {
    jsondm::Writer::member_guard g(w, "id");
    jsondm::write(w, id);
  }
  {
    jsondm::Writer::member_guard g(w, kind);
    w.append(payload);
  }
  w.obj_end();
  w.flush();
  return out;
}

std::string control_socket_t::parse_command(const std::string &command) {
  std::string method;
  std::optional<std::string> id;
  std::string_view params_span;
  try {
    // pre-scan: read the method
    {
      jsondm::Reader pre(command);
      pre.obj_begin();
      while (pre.next_key()) {
        if (pre.key() == "method") {
          jsondm::read(pre, method);
          break;
        }
        pre.skip_value();
      }
    }
    // full parse: id + params (the method key is skipped as unknown)
    {
      jsondm::Reader r(command);
      r.obj_begin();
      while (r.next_key()) {
        auto key = r.key();
        if (key == "method") {
          r.skip_value();
        } else if (key == "id") {
          jsondm::read(r, id);
        } else if (key == "params") {
          params_span = r.skip_value_span();
        } else {
          r.skip_value();
        }
      }
      r.obj_end();
    }
  } catch (const std::exception &e) {
    std::string out;
    jsondm::serialize(command_error_t{e.what()}, out);
    return out;
  }
  try {
    for (const auto &cmd : COMMANDS) {
      if (cmd.name == method) {
        std::string result;
        cmd.run(*this, params_span, result);
        return compose_response(id, "result", result);
      }
    }
    // if matches the regex (^\d*\..*), its a command to a peer
    std::smatch match;
    if (std::regex_match(method, match, PEER_COMMAND_RE)) {
      auto peer_id = std::stoi(match[1]);
      auto cmd = match[2];
      auto peer = router->get_peer_by_id(peer_id);
      if (peer) {
        std::string res = peer->command(cmd, params_span);
        if (auto err = extract_error(res)) {
          return compose_response(id, "error", *err);
        }
        return compose_response(id, "result", res);
      }
      return compose_response(id, "error",
                              FMT::format("Unknown peer '{}'", peer_id));
    }
    return compose_response(id, "error",
                            FMT::format("Unknown method '{}'", method));
  } catch (const std::exception &e) {
    return compose_response(id, "error", e.what());
  }
}
} // namespace rtpmididns
