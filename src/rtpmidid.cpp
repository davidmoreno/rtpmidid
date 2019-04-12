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
  setup_alsa_seq();
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
      peer_info->peer->on_midi([this, aseq_port](parse_buffer_t &pb){
        this->recv_rtpmidi_event(aseq_port, pb);
      });
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
    this->recv_alsamidi_event(aseq_port, ev);
  });
}


void ::rtpmidid::rtpmidid::recv_rtpmidi_event(int port, parse_buffer_t &midi_data){
  uint8_t current_command =  0;
  snd_seq_event_t ev;

  while (midi_data.position < midi_data.end){
    // MIDI may reuse the last command if appropiate. For example several consecutive Note On
    int maybe_next_command = midi_data.read_uint8();
    if (maybe_next_command & 0x80){
      current_command = maybe_next_command;
    } else {
      midi_data.position--;
    }
    auto type = current_command & 0xF0;

    switch(type){
      case 0xB0: // CC
      {
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_controller(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
      	snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq.seq, &ev);
      }
      break;
      case 0x90:
      {
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_noteon(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
      	snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq.seq, &ev);
      }
      break;
      case 0x80:
      {
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_noteoff(&ev, current_command & 0x0F, midi_data.read_uint8(), midi_data.read_uint8());
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
      	snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq.seq, &ev);
      }
      break;
      case 0xE0:
      {
        snd_seq_ev_clear(&ev);
        auto lsb = midi_data.read_uint8();
        auto msb = midi_data.read_uint8();
        snd_seq_ev_set_pitchbend(&ev, current_command & 0x0F,  (msb << 7) + lsb);
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
      	snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq.seq, &ev);
      }
      break;
      default:
        WARNING("MIDI command type {:02X} not implemented yet", type);
        return;
        break;
    }
  }

}


void ::rtpmidid::rtpmidid::recv_alsamidi_event(int aseq_port, snd_seq_event *ev){
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
}


void ::rtpmidid::rtpmidid::setup_alsa_seq(){
  // Export only one, but all data that is conencted to it.
  auto alsa_name = "Export A";
  auto aseq_port = seq.create_port(alsa_name);
  uint16_t netport = 0;

  auto peer_info = ::rtpmidid::peer_info{
    name, "localhost", netport, 0, nullptr,
  };

  auto rtpname = fmt::format("{}-A", name);
  peer_info.peer = std::make_shared<rtpserver>(rtpname, netport);
  peer_info.peer->on_midi([this, aseq_port](parse_buffer_t &pb){
    this->recv_rtpmidi_event(aseq_port, pb);
  });
  peer_info.use_count++;
  netport = peer_info.peer->local_base_port;
  peer_info.port = netport;


  seq.on_midi_event(aseq_port, [this, aseq_port](snd_seq_event_t *ev){
    this->recv_alsamidi_event(aseq_port, ev);
  });

  auto ptrname = fmt::format("_rtpmidid-{}._apple-midi._udp.local", rtpname);
  auto ptr = std::make_unique<mdns::service_ptr>(
      "_apple-midi._udp.local",
      300,
      ptrname
  );
  mdns.announce(std::move(ptr), true);

  auto srv = std::make_unique<mdns::service_srv>(
      ptrname,
      300,
      mdns.local(),
      netport
  );
  mdns.announce(std::move(srv), true);


  INFO("Listening RTP midi at {}:{}. It is ALSA port '{}'", peer_info.address, peer_info.port, alsa_name);

  known_peers[aseq_port] = std::move(peer_info);
}
