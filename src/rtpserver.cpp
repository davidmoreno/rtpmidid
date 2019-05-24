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
#include <unistd.h>

#include "./rtpserver.hpp"
#include "./poller.hpp"
#include "./exceptions.hpp"
#include "./logger.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

rtpserver::rtpserver(std::string _name, int16_t port) : name(std::move(_name)){
  control_socket = midi_socket = 0;
  control_port = port;
  midi_port = port + 1;

  try{
    struct sockaddr_in servaddr;
    control_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_socket < 0){
      throw rtpmidid::exception("Can not open control socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(control_port);
    if (bind(control_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open control socket. Maybe address is in use?");
    }
    if (control_port == 0){
      socklen_t len = sizeof(servaddr);
      ::getsockname(control_socket, (struct sockaddr*)&servaddr, &len);
      control_port = htons(servaddr.sin_port);
      DEBUG("Got automatic port {} for control", control_port);
      midi_port = control_port + 1;
    }

    poller.add_fd_in(control_socket, [this](int){ this->data_ready(rtppeer::CONTROL_PORT); });

    midi_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (midi_socket < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(midi_port);
    if (bind(midi_socket, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Maybe address is in use?");
    }
    poller.add_fd_in(midi_socket, [this](int){ this->data_ready(rtppeer::MIDI_PORT); });
  } catch (...){
    ERROR("Error creating server at port {}", control_port);
    if (control_socket){
      poller.remove_fd(control_socket);
      ::close(control_socket);
      control_socket = 0;
    }
    if (midi_socket){
      poller.remove_fd(midi_socket);
      ::close(midi_socket);
      midi_socket = 0;
    }
    throw;
  }

  INFO("Listening RTP MIDI connections at 0.0.0.0:{}, with name: '{}'",
    control_port, name);
}

rtpserver::~rtpserver(){
  if (control_socket > 0){
    poller.remove_fd(control_socket);
    close(control_socket);
  }
  if (midi_socket > 0){
    poller.remove_fd(midi_socket);
    close(midi_socket);
  }
}

std::shared_ptr<rtppeer> rtpserver::get_peer_by_packet(parse_buffer_t &buffer, rtppeer::port_e port){
  // Commands may be by SSRC or initiator_id
  auto command = rtppeer::commands_e((uint16_t(buffer.start[2])<<8) + buffer.start[3]);

  switch(command){
    case rtppeer::IN:
    case rtppeer::OK:
    case rtppeer::BY:
    case rtppeer::NO:
      {
        buffer.position = buffer.start + 8;
        auto initiator_id = buffer.read_uint32();
        buffer.position = buffer.start;
        return initiator_to_peer[initiator_id];
      }
    case rtppeer::CK:
    case rtppeer::RS:
    {
      buffer.position = buffer.start + 4;
      auto ssrc = buffer.read_uint32();
      buffer.position = buffer.start;
      return get_peer_by_ssrc(ssrc);
    }
    default:
      if (port == rtppeer::MIDI_PORT && buffer.start[1] == 0x61){
        buffer.read_uint32();
        buffer.read_uint32();
        auto ssrc = buffer.read_uint32();
        buffer.position = buffer.start;
        return get_peer_by_ssrc(ssrc);
      }
      DEBUG("COMMAND if {:X}", int(command));
      return nullptr;
  }
}

std::shared_ptr<rtppeer> rtpserver::get_peer_by_ssrc(uint32_t ssrc){
  auto peer = ssrc_to_peer[ssrc];
  if (peer)
    return peer;

  // If just connected, maybe we dont know the SSRC yet. Check all the peers to update
  for(auto &initiator_peer: initiator_to_peer){
    if (initiator_peer.second->remote_ssrc == ssrc){
      ssrc_to_peer[ssrc] = initiator_peer.second;
      return initiator_peer.second;
    }
  }
  return nullptr;
}

void rtpserver::data_ready(rtppeer::port_e port){
  uint8_t raw[1500];
  struct sockaddr_in cliaddr;
  unsigned int len = sizeof(cliaddr);
  auto socket = (port == rtppeer::CONTROL_PORT) ? control_socket : midi_socket;
  auto n = recvfrom(socket, raw, 1500, MSG_DONTWAIT, (struct sockaddr *) &cliaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0){
    auto netport = (port == rtppeer::CONTROL_PORT) ? control_port : midi_port;
    throw exception("Error reading from server 0.0.0.0:{}", netport);
  }

  auto buffer = parse_buffer_t(raw, n);

  auto peer = get_peer_by_packet(buffer, port);
  if (peer){
    peer->data_ready(buffer, port);
  } else {
    // If I dont know the other peer I'm only interested in IN, ignore others
    // If it is not a CONTROL PORT the messages come in the wrong order. The first IN should create the peer.
    if (port == rtppeer::CONTROL_PORT && rtppeer::is_command(buffer) && buffer.start[2] == 'I' && buffer.start[3] == 'N'){
      create_peer_from(buffer, &cliaddr);
    } else {
      DEBUG("Unknown peer, and not connect on control. Ignoring. Port {}", port == rtppeer::MIDI_PORT);
      buffer.print_hex(true);
    }
  }
}

void rtpserver::sendto(const parse_buffer_t &pb, rtppeer::port_e port, struct sockaddr_in *address, int remote_base_port){
  if (port == rtppeer::MIDI_PORT)
    address->sin_port = htons(remote_base_port + 1);
  else
    address->sin_port = htons(remote_base_port);

  auto socket = rtppeer::MIDI_PORT == port ? midi_socket : control_socket;

  // DEBUG("Send to {}, {}, family {} {}. {} {}", port, socket, AF_INET, address->sin_family, inet_ntoa(address->sin_addr), htons(address->sin_port));

  auto res = ::sendto(
    socket, pb.start, pb.length(),
    MSG_CONFIRM, (const struct sockaddr *)address, sizeof(struct sockaddr_in)
  );

  if (res != pb.length()){
    throw exception(
      "Could not send all data. Sent {}. {}",
      res, strerror(errno)
    );
  }
}


void rtpserver::create_peer_from(parse_buffer_t &buffer, struct sockaddr_in *cliaddr){
  auto peer = std::make_shared<rtppeer>(name);
  auto address = std::make_shared<struct sockaddr_in>();
  ::memcpy(address.get(), cliaddr, sizeof(struct sockaddr_in));
  auto remote_base_port = htons(cliaddr->sin_port);
  // DEBUG("Address family {} {}. From {}", cliaddr.sin_family, address->sin_family, socket);

  // This is the send to the proper ports
  peer->sendto = [this, address, remote_base_port](const parse_buffer_t &buff, rtppeer::port_e port){
    this->sendto(buff, port, address.get(), remote_base_port);
  };

  peer->data_ready(buffer, rtppeer::CONTROL_PORT);

  // After read the first packet I know the initiator_id
  initiator_to_peer[peer->initiator_id] = peer;

  // Setup some callbacks
  peer->on_connect([this, peer](const std::string &name){
    for(auto &f: connected_events){
      f(peer);
    }
  });

  peer->on_midi([this](parse_buffer_t &data){
    // DEBUG("Got MIDI from the remote peer into this server.");
    for (auto &f: midi_event_events){
      f(data);
    }
  });
}

void rtpserver::send_midi_to_all_peers(parse_buffer_t &buffer){
  for(auto &speers: ssrc_to_peer){
    speers.second->send_midi(buffer);
  }
}
