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
#include "./netutils.hpp"

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

void ::rtpmidid::rtpmidid::add_rtpmidi_client(const std::string &name, const std::string &address, uint16_t net_port){
  for (auto &known: known_peers){
    if (known.second.address == address && known.second.port == net_port){
      DEBUG(
          "Trying to add again rtpmidi {}:{} server. Quite probably mDNS re announce. "
          "Maybe somebody ask, or just periodically.", address, net_port
      );
      return;
    }
  }

  auto aseq_port = seq.create_port(name);
  auto peer_info = ::rtpmidid::peer_info{
    name, address, net_port, 0, nullptr,
  };

  INFO("New alsa port: {}, connects to {}:{} ({})", aseq_port, address, net_port, name);
  known_peers[aseq_port] = std::move(peer_info);

  seq.on_subscribe(aseq_port, [this, aseq_port](int client, int port, const std::string &name){
    DEBUG("Callback on subscribe at rtpmidid: {}", name);
    auto peer_info = &known_peers[aseq_port];
    if (!peer_info->peer){
      peer_info->peer = std::make_shared<rtpclient>(name, peer_info->address, peer_info->port);
      peer_info->use_count++;
    }
  });
  seq.on_unsubscribe(aseq_port, [this, aseq_port](int client, int port){
    DEBUG("Callback on unsubscribe at rtpmidid");
    auto peer_info = &known_peers[aseq_port];
    peer_info->use_count--;
    if (peer_info->use_count <= 0){
      peer_info->peer = nullptr;
    }
  });
  seq.on_midi_event(aseq_port, [this, aseq_port](snd_seq_event_t *ev){
    DEBUG("Callback on midi event at rtpmidid");
    auto peer_info = &known_peers[aseq_port];
    if (!peer_info->peer){
      ERROR("There is no peer but I received an event! This situation should NEVER happen. File a bug.");
      return;
    }
    uint8_t data[32];
    parse_buffer_t stream(data, sizeof(data));

    switch(ev->type){
      case SND_SEQ_EVENT_NOTE:
      case SND_SEQ_EVENT_NOTEON:
        if (ev->data.note.velocity == 0){
          stream.write_uint8(0x80 | (ev->data.note.channel & 0x0F));
        }
        else{
          stream.write_uint8(0x90 | (ev->data.note.channel & 0x0F));
        }
        stream.write_uint8(ev->data.note.note);
        stream.write_uint8(ev->data.note.velocity);
      break;
      case SND_SEQ_EVENT_NOTEOFF:
        stream.write_uint8(0x80 | (ev->data.note.channel & 0x0F));
        stream.write_uint8(ev->data.note.note);
        stream.write_uint8(ev->data.note.velocity);
      break;
      default:
        WARNING("Event type not yet implemented! Not sending. {}", ev->type);
        return;
        break;
    }

    stream.end = stream.position;
    stream.position = stream.start;

    peer_info->peer->send_midi(&stream);
  });
}
