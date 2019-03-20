/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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
#pragma once
#include <string>
#include <functional>
#include <map>
#include <fmt/format.h>
#include <arpa/inet.h>


namespace rtpmidid {
  class mdns {
  public:
    struct service{
      std::string service;
      std::string hostname;
      uint16_t port;
    };
    enum query_type_e{
      PTR=12,
      SRV=33,
      A=1,
      AAAA=28,
    };
  private:
    int socketfd;
    struct sockaddr_in multicast_addr;
    std::map<std::string, std::vector<std::function<void(const mdns::service &)>>> discovery_map;
  public:
    mdns();
    ~mdns();

    void on_discovery(const std::string &service, std::function<void(const mdns::service &)> f);
    void announce(const std::string &servicename);
    void query(const std::string &name, query_type_e type);
    void detected_service(const std::string_view &service, const std::string_view &hostname, uint16_t port);
    void mdns_ready();
  };
}

namespace std{
  std::string to_string(const rtpmidid::mdns::service &s);
}
