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

#include <iostream>
#include "./logger.hpp"
#include "./aseq.hpp"
#include "./rtpserver.hpp"
#include "./stringpp.hpp"
#include "./poller.hpp"
#include "./mdns.hpp"

using namespace std;


int main(int argc, char **argv){
    INFO("Real Time Protocol Music Industry Digital Interface Daemon - v0.1");
    INFO("(C) 2019 David Moreno Montero <dmoreno@coralbits.com>");

    try{
      auto seq = rtpmidid::aseq("rtpmidid");
      auto rtpserver = rtpmidid::rtpserver("rtpmidid", 15004);
      auto outputs = rtpmidid::get_ports(&seq);
      auto mdns = rtpmidid::mdns();

      mdns.on_discovery("_apple-midi._udp.local", rtpmidid::mdns::PTR, [&mdns](const rtpmidid::mdns::service *service){
        const rtpmidid::mdns::service_ptr *ptr = static_cast<const rtpmidid::mdns::service_ptr*>(service);
        INFO("Found apple midi response {}!", std::to_string(*ptr));
        mdns.on_discovery(ptr->servicename, rtpmidid::mdns::SRV, [&mdns](const rtpmidid::mdns::service *service){
          auto *srv = static_cast<const rtpmidid::mdns::service_srv*>(service);
          INFO("Found apple midi response {}!", std::to_string(*srv));
          int16_t port = srv->port;
          std::string name = srv->label.substr(0, srv->label.find('.'));
          mdns.query(srv->hostname, rtpmidid::mdns::A, [name, port](const rtpmidid::mdns::service *service){
            auto *ip = static_cast<const rtpmidid::mdns::service_a*>(service);
            auto ip4 = ip->ip;
            INFO("APPLE MIDI: {}, at {}.{}.{}.{}:{}", name, uint8_t(ip4[0]), uint8_t(ip4[1]), uint8_t(ip4[2]), uint8_t(ip4[3]), port);
          });
        });
      });

      auto ptr = std::make_unique<rtpmidid::mdns::service_ptr>();
      ptr->label = "_apple-midi._udp.local";
      ptr->type = rtpmidid::mdns::PTR;
      ptr->servicename = "rtpmidid._apple-midi._udp.local";
      mdns.announce(std::move(ptr));

      auto srv = std::make_unique<rtpmidid::mdns::service_srv>();
      srv->label = "rtpmidid._apple-midi._udp.local";
      srv->type = rtpmidid::mdns::SRV;
      srv->hostname = "ucube.local";
      srv->port = 15004;
      mdns.announce(std::move(srv));

      // auto a = std::make_unique<rtpmidid::mdns::service_a>();
      // a->label = "ucube.local";
      // a->type = rtpmidid::mdns::SRV;
      // a->ip[0] = 192;
      // a->ip[1] = 168;
      // a->ip[2] = 1;
      // a->ip[3] = 131;
      // mdns.announce(std::move(a));

      DEBUG("ALSA seq ports: {}", std::to_string(outputs));

      for(;;){
        rtpmidid::poller.wait();
      }
    } catch (const std::exception &e){
      ERROR("{}", e.what());
      return 1;
    }
    DEBUG("FIN");
    return 0;
}
