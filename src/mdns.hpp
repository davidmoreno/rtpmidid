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
#pragma once
#include <string>
#include <functional>
#include <map>
#include <fmt/format.h>
#include <arpa/inet.h>
#include "./exceptions.hpp"
#include "./poller.hpp"


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
      rtpmidid::poller_t::timer_t cache_timeout_id;
      std::string label;
      query_type_e type;
      uint32_t ttl;

      service();
      service(std::string label_, query_type_e type_, uint32_t ttl_);
      virtual ~service(){}
      virtual std::unique_ptr<service> clone() const;
      virtual bool equal(const service *other);
      bool base_equal(const service *other);
      virtual std::string to_string() const;
    };
    struct service_a : public service {
      union{
        // If 0.0.0.0 I will answer my own IP
        uint8_t ip[4];
        uint32_t ip4;
      };

      service_a();
      service_a(std::string label_, uint32_t ttl_, uint8_t ip_[4]);
      virtual std::unique_ptr<service> clone() const;
      virtual bool equal(const service *other_);
      virtual std::string to_string() const;
    };
    struct service_srv : public service {
      std::string hostname;
      uint16_t port;

      service_srv();
      service_srv(std::string label_, uint32_t ttl_, std::string hostname_, uint16_t port_);
      virtual std::unique_ptr<service> clone() const;
      virtual bool equal(const service *other_);
      virtual std::string to_string() const;
    };
    struct service_ptr : public service {
      std::string servicename;

      service_ptr();
      service_ptr(std::string label_, uint32_t ttl_, std::string servicename_);
      virtual std::unique_ptr<service> clone() const;
      virtual bool equal(const service *other_);
      virtual std::string to_string() const;
    };

  private:
    int socketfd;
    struct sockaddr_in multicast_addr;
    // queries are called once
    std::map<std::pair<query_type_e, std::string>, std::vector<std::function<void(const service *)>>> query_map;
    // discovery are called always there is a discovery
    std::map<std::pair<query_type_e, std::string>, std::vector<std::function<void(const service *)>>> discovery_map;
    // I know about all this entries, just in case somebody asks
    std::map<std::pair<query_type_e, std::string>,  std::vector<std::unique_ptr<service>>> announcements;
    // Cache data.
    std::map<std::pair<query_type_e, std::string>,  std::vector<std::unique_ptr<service>>> cache;
    // Reannounce timers, with associated announcement service pointers
    std::map<service *, rtpmidid::poller_t::timer_t> reannounce_timers;
    // Cache timers, with associated announcement service pointers
    std::map<service *, rtpmidid::poller_t::timer_t> cache_timers;
    // IP to answer for A queries with 0.0.0.0 answer (marker for myself)
    uint8_t ip4[4];
  public:
    mdns();
    ~mdns();

    void query(const std::string &name, query_type_e type);
    // Calls once the service function with the service
    void query(const std::string &name, query_type_e type, std::function<void(const service *)>);
    // Calls everytime we get a service with the given parameters
    void on_discovery(const std::string &service, query_type_e type, std::function<void(const mdns::service *)> f);
    // Not interested anymore in this type
    void remove_discovery(const std::string &service, query_type_e type);
    // A service is detected, call query and discovery services
    void detected_service(const service *res);

    void announce(std::unique_ptr<service>, bool broadcast=false);
    void reannounce_later(service *);
    // Sends the goodbye mDNS packet and removes from current announcements list
    void unannounce(service *);
    bool answer_if_known(mdns::query_type_e type_, const std::string &label);
    void send_response(const service &);
    void mdns_ready();
    void update_cache(const service *service);
    void remove_cache_timer(const service *service);

    // The local name as can be resolved.
    std::string local();
  };
}

namespace std{
  std::string to_string(const rtpmidid::mdns::service_a &s);
  std::string to_string(const rtpmidid::mdns::service_ptr &s);
  std::string to_string(const rtpmidid::mdns::service_srv &s);
}
