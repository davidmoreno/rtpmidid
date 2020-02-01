/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "../third_party/nlohmann/json.hpp"

#include "poller.hpp"
#include "logger.hpp"
#include "stringpp.hpp"
#include "config.hpp"
#include "control_socket.hpp"

using json = nlohmann::json;

// Commands
static json stats();

const char *MSG_CLOSE_CONN = "{\"method\": \"close\", \"params\": {\"detail\": \"Shutdown\", \"code\": 0}}\n";
const char *MSG_TOO_LONG = "{\"method\": \"close\", \"params\": {\"detail\": \"Message too long\", \"code\": 1}}\n";
const char *MSG_UNKNOWN_COMMAND = "{\"id\": 0, \"error\": {\"detail\": \"Unknown command\", \"code\": 2}}";

rtpmidid::control_socket_t::control_socket_t(const std::string &socketfile){
    int ret;
    ret = unlink(socketfile.c_str());
    if (ret>=0){
        INFO("Removed old control socket. Creating new one.");
    }

    listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketfile.c_str(), sizeof(addr.sun_path) - 1);

    ret = bind(listen_socket, (const struct sockaddr *)(&addr), sizeof(struct sockaddr_un));
    if (ret == -1){
        ERROR("Error Listening to socket: {}", strerror(errno));
        close(listen_socket);
        listen_socket = -1;
        return;
    }
    listen(listen_socket, 20);
    rtpmidid::poller.add_fd_in(listen_socket, [this](int fd) {
        this->connection_ready();
    });
    INFO("Control socket ready at {}", socketfile);
}

rtpmidid::control_socket_t::~control_socket_t(){
    for(auto fd: clients){
        write(fd, MSG_CLOSE_CONN, strlen(MSG_CLOSE_CONN));
        close(fd);
    }
    close(listen_socket);
}

void rtpmidid::control_socket_t::connection_ready(){
    int fd = accept(listen_socket, NULL, NULL);

    rtpmidid::poller.add_fd_in(fd, [this](int fd) {
        this->data_ready(fd);
    });
    DEBUG("Added control connection: {}", fd);
    clients.push_back(fd);
}

void rtpmidid::control_socket_t::data_ready(int fd){
    char buf[1024];
    int l = recv(fd, buf, sizeof(buf), 0);
    if (l <= 0){
        close(fd);
        DEBUG("Closed control connection: {}", fd);
        clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
        return;
    }
    if (l >= sizeof(buf) - 1){
        write(fd, MSG_TOO_LONG, strlen(MSG_TOO_LONG));
        return;
    }
    buf[l] = 0;
    auto ret = parse_command(rtpmidid::trim_copy(buf));
    ret += "\n";
    write(fd, ret.c_str(), ret.length());
    fsync(fd);
}

std::string rtpmidid::control_socket_t::parse_command(const std::string &command){
    DEBUG("Received command: {}", command);
    if (command == "STATS") {
        auto js = stats();
        return js.dump();
    }
    return MSG_UNKNOWN_COMMAND;
}



// Commands

static json stats(){
    return {
        {"version", rtpmidid::VERSION},
        {"uptime", 0}
    };
}
