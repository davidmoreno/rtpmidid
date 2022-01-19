/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "mdns.hpp"
#include <fstream>
#include <iostream>
#include <rtpmidid/mdns_custom.hpp>

const int TIMEOUT_REANNOUNCE = 75 * 60; // As recommended by RFC 6762
std::string hostname;

static ::mdns::mdns mdns_singleton;

rtpmidid::mdns::mdns() { setup_mdns_browser(); }

rtpmidid::mdns::~mdns() {}

void rtpmidid::mdns::setup_mdns_browser() {
  std::ifstream("/etc/hostname") >> hostname;
  INFO("Hostname {}", hostname);

  mdns_singleton.on_discovery(
      "_apple-midi._udp.local", ::mdns::mdns::PTR,
      [](const ::mdns::mdns::service *service) {
        const ::mdns::mdns::service_ptr *ptr =
            static_cast<const ::mdns::mdns::service_ptr *>(service);
        // INFO("Found apple midi PTR response {}!", std::to_string(*ptr));
        // just ask, next on discovery will catch it.
        mdns_singleton.query(ptr->servicename, ::mdns::mdns::SRV);
      });

  mdns_singleton.query("_apple-midi._udp.local", ::mdns::mdns::PTR);

  mdns_singleton.on_discovery(
      "*._apple-midi._udp.local", ::mdns::mdns::SRV,
      [this](const ::mdns::mdns::service *service) {
        if (service->ttl == 0) // This is a removal, not interested
          return;

        auto *srv = static_cast<const ::mdns::mdns::service_srv *>(service);
        uint16_t port = srv->port;
        std::string srvname = srv->label;

        INFO("Found apple midi SRV response {}!", std::to_string(*srv));
        mdns_singleton.query(
            srv->hostname, ::mdns::mdns::A,
            [this, srvname, port](const ::mdns::mdns::service *service) {
              auto name = srvname.substr(0, srvname.find('.'));
              auto *ip = static_cast<const ::mdns::mdns::service_a *>(service);
              const uint8_t *ip4 = ip->ip;
              std::string address =
                  fmt::format("{}.{}.{}.{}", uint8_t(ip4[0]), uint8_t(ip4[1]),
                              uint8_t(ip4[2]), uint8_t(ip4[3]));
              INFO("APPLE MIDI: {}, at {}:{}", name, address, port);

              discover_event(name, srvname, std::to_string(port));
            });
      });
}

void rtpmidid::mdns::announce_all() {}

void rtpmidid::mdns::announce_rtpmidi(const std::string &name,
                                      const int32_t port) {

  auto ptr = std::make_unique<::mdns::mdns::service_ptr>();
  ptr->label = "_apple-midi._udp.local";
  ptr->ttl = TIMEOUT_REANNOUNCE;
  ptr->type = ::mdns::mdns::PTR;
  ptr->servicename = fmt::format("{}._apple-midi._udp.local", name);
  mdns_singleton.announce(std::move(ptr), true);

  auto hostname = fmt::format("rtpmidid-{}.local", ::hostname);
  auto srv = std::make_unique<::mdns::mdns::service_srv>();
  srv->label = fmt::format("{}._apple-midi._udp.local", name);
  srv->ttl = TIMEOUT_REANNOUNCE;
  srv->type = ::mdns::mdns::SRV;
  srv->hostname = hostname;
  srv->port = port;
  mdns_singleton.announce(std::move(srv), true);

  auto a = std::make_unique<::mdns::mdns::service_a>();
  a->label = hostname;
  a->ttl = TIMEOUT_REANNOUNCE;
  a->type = ::mdns::mdns::A;
  a->ip4 = 0;
  mdns_singleton.announce(std::move(a), true);

  auto txt = std::make_unique<::mdns::mdns::service_txt>();
  txt->label = hostname;
  txt->ttl = TIMEOUT_REANNOUNCE;
  txt->type = ::mdns::mdns::TXT;
  txt->txt = "";
  mdns_singleton.announce(std::move(txt), true);
}
void rtpmidid::mdns::unannounce_rtpmidi(const std::string &name,
                                        const int32_t port) {
  ::mdns::mdns::service_ptr ptr;
  ptr.label = "_apple-midi._udp.local";
  ptr.ttl = 0; // This means, remove
  ptr.type = ::mdns::mdns::PTR;
  ptr.servicename = fmt::format("{}._apple-midi._udp.local", name);
  mdns_singleton.unannounce(&ptr);

  auto hostname = fmt::format("rtpmidid-{}.local", ::hostname);
  ::mdns::mdns::service_srv srv;
  srv.label = fmt::format("{}._apple-midi._udp.local", name);
  srv.ttl = 0;
  srv.type = ::mdns::mdns::SRV;
  srv.hostname = hostname;
  srv.port = port;
  mdns_singleton.unannounce(&srv);

  auto a = std::make_unique<::mdns::mdns::service_a>();
  a->label = hostname;
  a->ttl = 0;
  a->type = ::mdns::mdns::A;
  a->ip4 = 0;
  mdns_singleton.announce(std::move(a), true);
}
