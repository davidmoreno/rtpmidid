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
    enum query_type_e{
      PTR=12,
      SRV=33,
      A=1,
      AAAA=28,
    };

    struct service{
      std::string label;
      query_type_e type;
    };
    struct service_a : public service {
      uint8_t ip[4];
    };
    struct service_srv : public service {
      std::string hostname;
      uint16_t port;
    };
    struct service_ptr : public service {
      std::string servicename;
    };

  private:
    int socketfd;
    struct sockaddr_in multicast_addr;
    // queries are called once
    std::map<std::pair<query_type_e, std::string>, std::vector<std::function<void(const service *)>>> query_map;
    // discovery are called always there is a discovery
    std::map<std::pair<query_type_e, std::string>, std::vector<std::function<void(const service *)>>> discovery_map;
  public:
    mdns();
    ~mdns();

    void query(const std::string &name, query_type_e type);
    // Calls once the service function with the service
    void query(const std::string &name, query_type_e type, std::function<void(const service *)>);
    // Calls everytime we get a service with the given parameters
    void on_discovery(const std::string &service, query_type_e type, std::function<void(const mdns::service *)> f);
    // A service is detected, call query and discovery services
    void detected_service(const service *res);

    void announce(const std::string &servicename);
    void mdns_ready();
  };
}

namespace std{
  std::string to_string(const rtpmidid::mdns::service_a &s);
  std::string to_string(const rtpmidid::mdns::service_ptr &s);
  std::string to_string(const rtpmidid::mdns::service_srv &s);
}
