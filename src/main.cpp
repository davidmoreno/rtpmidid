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
#include "./rtpport.hpp"
#include "./stringpp.hpp"
#include "./poller.hpp"
#include "./mdns.hpp"

using namespace std;


int main(int argc, char **argv){
    INFO("Real Time Protocol Music Industry Digital Interface Daemon - v0.1");
    INFO("(C) 2019 David Moreno Montero <dmoreno@coralbits.com>");

    try{
      auto seq = rtpmidid::aseq("rtpmidid");
      auto rtpport = rtpmidid::rtpport("rtpmidid", 15004);
      auto outputs = rtpmidid::get_ports(&seq);
      auto mdns = rtpmidid::mdns();

      mdns.on_discovery("_apple-midi._udp.local", rtpmidid::mdns::PTR, [&mdns](const rtpmidid::mdns::service *service){
        const rtpmidid::mdns::service_ptr *ptr = static_cast<const rtpmidid::mdns::service_ptr*>(service);
        INFO("Found apple midi response {}!", std::to_string(*ptr));
        mdns.on_discovery(ptr->servicename, rtpmidid::mdns::SRV, [&mdns](const rtpmidid::mdns::service *service){
          auto *srv = static_cast<const rtpmidid::mdns::service_srv*>(service);
          INFO("Found apple midi response {}!", std::to_string(*srv));
          int16_t port = srv->port;
          mdns.query(srv->hostname, rtpmidid::mdns::A, [port](const rtpmidid::mdns::service *service){
            auto *ip = static_cast<const rtpmidid::mdns::service_a*>(service);
            auto ip4 = ip->ip;
            INFO("APPLE MIDI at {}.{}.{}.{}:{}", uint8_t(ip4[0]), uint8_t(ip4[1]), uint8_t(ip4[2]), uint8_t(ip4[3]), port);
          });
        });
      });

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
