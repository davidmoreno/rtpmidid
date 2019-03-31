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

#include "./rtpmidid.hpp"
#include "./aseq.hpp"
#include "./rtpserver.hpp"
#include "./rtpclient.hpp"
#include "./logger.hpp"

using namespace rtpmidid;

::rtpmidid::rtpmidid::rtpmidid(std::string &&_name) : name(_name), seq(name){
  auto outputs = ::rtpmidid::get_ports(&seq);

  setup_mdns();
}

void ::rtpmidid::rtpmidid::add_rtpmidid_server(const std::string &name){
  auto rtpserver = ::rtpmidid::rtpserver(name, 0);
  auto port = rtpserver.local_base_port;

  auto ptr = std::make_unique<::rtpmidid::mdns::service_ptr>();
  ptr->label = "_apple-midi._udp.local";
  ptr->type = ::rtpmidid::mdns::PTR;
  ptr->servicename = fmt::format("{}._apple-midi._udp.local", name);
  mdns.announce(std::move(ptr));

  auto srv = std::make_unique<::rtpmidid::mdns::service_srv>();
  srv->label = fmt::format("{}._apple-midi._udp.local", name);
  srv->type = ::rtpmidid::mdns::SRV;
  srv->hostname = "ucube.local";
  srv->port = port;
  mdns.announce(std::move(srv));
}


void ::rtpmidid::rtpmidid::setup_mdns(){
  mdns.on_discovery("_apple-midi._udp.local", mdns::PTR, [this](const ::rtpmidid::mdns::service *service){
    const ::rtpmidid::mdns::service_ptr *ptr = static_cast<const ::rtpmidid::mdns::service_ptr*>(service);
    INFO("Found apple midi response {}!", std::to_string(*ptr));
    mdns.on_discovery(ptr->servicename, ::rtpmidid::mdns::SRV, [this](const ::rtpmidid::mdns::service *service){
      auto *srv = static_cast<const ::rtpmidid::mdns::service_srv*>(service);
      INFO("Found apple midi response {}!", std::to_string(*srv));
      int16_t port = srv->port;
      std::string name = srv->label.substr(0, srv->label.find('.'));
      mdns.query(srv->hostname, ::rtpmidid::mdns::A, [this, name, port](const ::rtpmidid::mdns::service *service){
        auto *ip = static_cast<const ::rtpmidid::mdns::service_a*>(service);
        const uint8_t *ip4 = ip->ip;
        std::string address = fmt::format("{}.{}.{}.{}", uint8_t(ip4[0]), uint8_t(ip4[1]), uint8_t(ip4[2]), uint8_t(ip4[3]));
        INFO("APPLE MIDI: {}, at {}:{}", name, address, port);

        this->add_rtpmidi_client(name, address, port);
      });
    });
  });

}

void ::rtpmidid::rtpmidid::add_rtpmidi_client(const std::string &name, const std::string &address, uint16_t port){
  auto aseq_port = seq.create_port(name);
  auto peer_info = ::rtpmidid::peer_info{
    name, address, port, nullptr,
  };

  INFO("New alsa port: {}, connects to {}:{} ({})", aseq_port, address, port, name);
  known_peers[aseq_port] = std::move(peer_info);
}
