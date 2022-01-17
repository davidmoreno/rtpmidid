/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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

#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../third_party/nlohmann/json.hpp"

#include "./rtpmidid.hpp"
#include "config.hpp"
#include "control_socket.hpp"
#include "stringpp.hpp"
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/rtppeer.hpp>
#include <rtpmidid/rtpserver.hpp>

using json = nlohmann::json;

const char *MSG_CLOSE_CONN =
    "{\"event\": \"close\", \"detail\": \"Shutdown\", \"code\": 0}\n";
const char *MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";
const char *MSG_UNKNOWN_COMMAND =
    "{\"error\": \"Unknown command\", \"code\": 2}";

struct control_msg_t {
  int id;
  std::string method;
  json params;
};

rtpmidid::control_socket_t::control_socket_t(rtpmidid::rtpmidid_t &rtpmidid,
                                             const std::string &socketfile)
    : rtpmidid(rtpmidid) {
  int ret = unlink(socketfile.c_str());
  if (ret >= 0) {
    INFO("Removed old control socket. Creating new one.");
  }

  listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_socket == -1) {
    ERROR("Error creating socket: {}", strerror(errno));
    return;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);

  ret = bind(listen_socket, (const struct sockaddr *)(&addr),
             sizeof(struct sockaddr_un));
  if (ret == -1) {
    ERROR("Error Binding socket at {}: {}", socketfile, strerror(errno));
    close(listen_socket);
    listen_socket = -1;
    return;
  }
  if (listen(listen_socket, 20) == -1) {
    ERROR("Error Listening to socket at {}: {}", socketfile, strerror(errno));
    close(listen_socket);
    listen_socket = -1;
    return;
  }
  ::chmod(socketfile.c_str(), 0777);
  rtpmidid::poller.add_fd_in(listen_socket,
                             [this](int fd) { this->connection_ready(); });
  INFO("Control socket ready at {}", socketfile);
  start_time = time(NULL);
}

rtpmidid::control_socket_t::~control_socket_t() {
  for (auto fd : clients) {
    auto n = write(fd, MSG_CLOSE_CONN, strlen(MSG_CLOSE_CONN));
    if (n < 0) {
      DEBUG("Could not send goodbye packet to control.");
    }
    close(fd);
  }
  close(listen_socket);
}

void rtpmidid::control_socket_t::connection_ready() {
  int fd = accept(listen_socket, NULL, NULL);

  if (fd != -1) {
    rtpmidid::poller.add_fd_in(fd, [this](int fd) { this->data_ready(fd); });
    DEBUG("Added control connection: {}", fd);
    clients.push_back(fd);
  } else {
    ERROR("\"accept()\" failed, continuing...");
  }
}

void rtpmidid::control_socket_t::data_ready(int fd) {
  char buf[1024];
  size_t l = recv(fd, buf, sizeof(buf), 0);
  if (l <= 0) {
    close(fd);
    DEBUG("Closed control connection: {}", fd);
    clients.erase(std::remove(clients.begin(), clients.end(), fd),
                  clients.end());
    return;
  }
  if (l >= sizeof(buf) - 1) {
    auto w = write(fd, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
    if (w < 0) {
      ERROR(
          "Could not send msg too long to control socket! Closing connection.");
      ::close(fd);
      fd = -1;
    }
    return;
  }
  buf[l] = 0;
  auto ret = parse_command(rtpmidid::trim_copy(buf));
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

namespace rtpmidid {
namespace commands {
// Commands
static json status(rtpmidid::rtpmidid_t &rtpmidid, time_t start_time) {
  auto js =
      json{{"version", rtpmidid::VERSION}, {"uptime", time(NULL) - start_time}};

  std::vector<json> clients;
  for (auto port_client : rtpmidid.known_clients) {
    auto client = port_client.second;
    json cl = {{"name", client.name},
               {"use_count", client.use_count},
               {"alsa_port", port_client.first}};
    std::vector<json> addresses;
    for (auto &address : client.addresses) {
      addresses.push_back({{"host", address.address}, {"port", address.port}});
    }
    cl["addresses"] = addresses;
    if (client.peer) {
      auto peer = client.peer;
      cl["latency_ms"] = peer->peer.latency / 10.0;
      cl["sequence_number"] = peer->peer.seq_nr;
      cl["sequence_number_ack"] = peer->peer.seq_nr_ack;
      cl["sequence_remote"] = peer->peer.remote_seq_nr;
    }
    clients.push_back(cl);
  }
  js["clients"] = clients;

  std::vector<json> connections;
  for (auto port_client : rtpmidid.known_servers_connections) {
    auto client = port_client.second;
    json cl = {
        {"name", client.name},
    };
    clients.push_back(cl);
  }
  js["connections"] = connections;

  std::vector<json> servers;
  for (auto server : rtpmidid.servers) {
    json data = {
        {"name", server->name},
        {"port", server->midi_port},
        {"connect_listeners", server->connected_event.count()},
        {"midi_listeners", server->midi_event.count()},
    };
    servers.push_back(data);
  }
  js["servers"] = servers;

  return js;
}

static json exit(rtpmidid::rtpmidid_t &rtpmidid) {
  rtpmidid::poller.close();
  return {{"detail", "Bye."}};
}

static json connect(rtpmidid::rtpmidid_t &rtpmidid, const std::string &name,
                    const std::string &host, const std::string &port) {
  auto alsa_port = rtpmidid.add_rtpmidi_client(name, host, port);
  if (alsa_port.has_value()) {
    return {
        {"alsa_port", alsa_port.value()},
    };
  } else {
    return {{"detail", "Could not connect. Check logs."}};
  }
}
} // namespace commands
} // namespace rtpmidid

// Last declaration to avoid forward declaration

std::string
rtpmidid::control_socket_t::parse_command(const std::string &command) {
  control_msg_t msg;
  msg.id = 0;
  DEBUG("Received command: {}", command);
  if (command.length() == 0) {
    return MSG_UNKNOWN_COMMAND;
  }
  if (std::startswith(command, "{")) {
    auto js = json::parse(command);
    msg.method = js["method"];
    msg.params = js["params"];
    if (js.contains("id"))
      msg.id = js["id"];
  } else {
    auto command_split = rtpmidid::split(command);
    msg.method = command_split[0];
    std::vector<json> params;
    for (size_t i = 1; i < command_split.size(); i++) {
      params.push_back(command_split[i]);
    }
    msg.params = params;
  }
  json ret = nullptr; // Fill the one you return
  json error =
      json{{"detail", "Unknown command"}, {"code", 2}}; // By detault no command

  if (msg.method == "status") {
    ret = rtpmidid::commands::status(rtpmidid, start_time);
  }
  if (msg.method == "exit" || msg.method == "quit") {
    ret = rtpmidid::commands::exit(rtpmidid);
  }
  if (msg.method == "connect") {
    switch (msg.params.size()) {
    case 1:
      ret = rtpmidid::commands::connect(rtpmidid, msg.params[0], msg.params[0],
                                        "5004");
      break;
    case 2:
      ret = rtpmidid::commands::connect(rtpmidid, msg.params[0], msg.params[0],
                                        msg.params[1]);
      break;
    case 3:
      ret = rtpmidid::commands::connect(rtpmidid, msg.params[0], msg.params[1],
                                        msg.params[2]);
      break;
    default:
      error = {{"detail", "Invalid params"}, {"code", 3}};
    }
  }
  if (msg.method == "update-mdns") {
    rtpmidid.mdns.setup_mdns_browser();
    ret = {{"detail", "mDNS update requested"}};
  }
  if (msg.method == "help") {
    ret = json{{"commands", {"help", "exit", "connect", "status"}}};
  }

  json retdata = {{"id", msg.id}};
  if (!ret.is_null()) {
    retdata["result"] = ret;
  } else {
    retdata["error"] = error;
  }
  return retdata.dump();
}
