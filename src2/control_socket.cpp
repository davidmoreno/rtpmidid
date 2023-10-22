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
#include "config.hpp"
#include "factory.hpp"
#include "settings.hpp"
#include <algorithm>
#include <functional>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "json.hpp"
#include "midipeer.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/poller.hpp"
#include "stringpp.hpp"

namespace rtpmididns {

extern const char *VERSION;
const char *MSG_CLOSE_CONN =
    "{\"event\": \"close\", \"detail\": \"Shutdown\", \"code\": 0}\n";
const char *MSG_TOO_LONG =
    "{\"event\": \"close\", \"detail\": \"Message too long\", \"code\": 1}\n";
const char *MSG_UNKNOWN_COMMAND =
    "{\"error\": \"Unknown command\", \"code\": 2}";

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
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);

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

rtpmididns::control_socket_t::~control_socket_t() {
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
}

void rtpmididns::control_socket_t::connection_ready() {
  int fd = accept(socket, NULL, NULL);

  if (fd != -1) {
    client_t client;
    client.listener = rtpmidid::poller.add_fd_in(
        fd, [this](int fd) { this->data_ready(fd); });
    client.fd = fd;
    clients.push_back(std::move(client));
    DEBUG("Added control connection: {}", fd);
  } else {
    ERROR("\"accept()\" failed, continuing...");
  }
}

void control_socket_t::data_ready(int fd) {
  char buf[1024];
  size_t l = recv(fd, buf, sizeof(buf), 0);
  if (l <= 0) {
    DEBUG("Closed control connection: {}", fd);
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
      fd = -1;
    }
    return;
  }
  buf[l] = 0;
  auto ret = parse_command(trim_copy(buf));
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

namespace control_socket_ns {
struct command_t {
  const char *name;
  std::function<json_t(rtpmididns::control_socket_t &, const json_t &)> func;
};
} // namespace control_socket_ns
std::vector<control_socket_ns::command_t> commands{
    {"status",
     [](control_socket_t &control, const json_t &) {
       return json_t{
           {"version", rtpmididns::VERSION},
           {"settings",
            {
                {"alsa_name", rtpmididns::settings.alsa_name},
                {"rtpmidid_name", rtpmididns::settings.rtpmidid_name},
                {"rtpmidid_port", rtpmididns::settings.rtpmidid_port},
                {"control_filename", rtpmididns::settings.control_filename} //
            }},
           {"router", control.router->status()} //
       };
     }},
    {"router.remove",
     [](control_socket_t &control, const json_t &params) {
       DEBUG("Params {}", params.dump());
       peer_id_t peer_id;
       peer_id = params[0];
       DEBUG("Remove peer_id {}", peer_id);
       control.router->remove_peer(peer_id);
       return "ok";
     }},
    {"connect",
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
                       "Need 1 param (hostn ame:hostname:5004), 2 params "
                       "(hostname:port), "
                       "3 params (name,hostname,port) or a dict{name, "
                       "hostname, port}"};

       control.router->add_peer(
           make_alsawaiter(name, hostname, port, control.aseq));
       return json_t{"ok"};
     }}
    //
};

std::string control_socket_t::parse_command(const std::string &command) {
  DEBUG("Parse command {}", command);
  auto js = json_t::parse(command);

  auto method = js["method"];
  try {
    if (method == "list") {
      auto res = std::vector<std::string>{};
      for (const auto &cmd : commands) {
        res.push_back(cmd.name);
      }
      json_t retdata = {
          {"id", js["id"]}, {"result", res},
          //
      };

      return retdata.dump();
    }
    for (const auto &cmd : commands) {
      if (cmd.name == method) {
        auto res = cmd.func(*this, js["params"]);
        json_t retdata = {
            {"id", js["id"]}, {"result", res},
            //
        };

        return retdata.dump();
      }
    }
    json_t retdata = {
        {"id", js["id"]}, {"error", fmt::format("Unknown method '{}'", method)},
        //
    };

    return retdata.dump();
  } catch (const std::exception &e) {
    json_t retdata = {{"id", js["id"]}, {"error", e.what()}};
    return retdata.dump();
  }
}
} // namespace rtpmididns
