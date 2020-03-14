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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "./rtpserver.hpp"
#include "./poller.hpp"
#include "./exceptions.hpp"
#include "./logger.hpp"
#include "./netutils.hpp"

using namespace rtpmidid;

rtpserver::rtpserver(std::string _name, const std::string &port) : name(std::move(_name)){
  control_socket = midi_socket = 0;
  control_port = 0;
  midi_port = 0;
  struct addrinfo *sockaddress_list = nullptr;

  try{
    struct addrinfo hints;

    int res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    const char *cport = (port == "") ? nullptr : port.c_str();

    res = getaddrinfo("::", cport, &hints, &sockaddress_list);
    if (res < 0) {
      DEBUG("Error resolving address {}:{}", "::", port);
      throw rtpmidid::exception("Can not resolve address {}:{}. {}", "::", port, strerror(errno));
    }
    // Get addr info may return several options, try them in order.
    // we asusme that if the ocntrol success to be created the midi will too.
    char host[NI_MAXHOST], service[NI_MAXSERV];
    socklen_t peer_addr_len = NI_MAXHOST;
    auto listenaddr = sockaddress_list;
    for(; listenaddr != nullptr; listenaddr = listenaddr->ai_next) {
      getnameinfo(
        listenaddr->ai_addr,
        peer_addr_len, host, NI_MAXHOST,
        service, NI_MAXSERV, NI_NUMERICSERV
      );
      DEBUG("Try listen at {}:{}", host, service);

      control_socket = socket(listenaddr->ai_family, listenaddr->ai_socktype, listenaddr->ai_protocol);
      if (control_socket < 0){
        continue; // Bad socket. Try next.
      }
      if (bind(control_socket, listenaddr->ai_addr, listenaddr->ai_addrlen) == 0){
        break;
      }
      close(control_socket);
    }
    if (!listenaddr){
      throw rtpmidid::exception("Can not open rtpmidi control socket. {}.", strerror(errno));
    }
    struct sockaddr_in6 addr;
    unsigned int len = sizeof(addr);
    res = ::getsockname(control_socket, (sockaddr*)&addr, &len);
    if (res < 0){
      throw rtpmidid::exception("Error getting info the newly created midi socket. Can not create server.");
    }
    control_port = ntohs(addr.sin6_port);

    DEBUG("Control port at {}:{}", host, control_port);
    midi_port = control_port + 1;

    poller.add_fd_in(control_socket, [this](int){ this->data_ready(rtppeer::CONTROL_PORT); });

    midi_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (midi_socket < 0){
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    // Reuse listenaddr, just on next port
    ((sockaddr_in*)listenaddr->ai_addr)->sin_port = htons( midi_port );
    if (bind(midi_socket, listenaddr->ai_addr, listenaddr->ai_addrlen) < 0){
      throw rtpmidid::exception("Can not open MIDI socket. {}.", strerror(errno));
    }
    poller.add_fd_in(midi_socket, [this](int){ this->data_ready(rtppeer::MIDI_PORT); });
  } catch (const std::exception &e){
    ERROR("Error creating server at port {}: {}", control_port, e.what());
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
    if (sockaddress_list){
      freeaddrinfo(sockaddress_list);
    }
    throw;
  }
  if (sockaddress_list){
    freeaddrinfo(sockaddress_list);
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
  struct sockaddr_in6 cliaddr;
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
    if (rtppeer::is_command(buffer) && buffer.start[2] == 'I' && buffer.start[3] == 'N'){
      create_peer_from(buffer, &cliaddr, port);
    } else {
      DEBUG("Unknown peer, and not connect on control. Ignoring. {} port.", port == rtppeer::MIDI_PORT ? "MIDI" : "Control");
      buffer.print_hex(true);
    }
  }
}

void rtpserver::sendto(const parse_buffer_t &pb, rtppeer::port_e port, struct sockaddr_in6 *address, int remote_base_port){
  if (port == rtppeer::MIDI_PORT)
    address->sin6_port = htons(remote_base_port + 1);
  else
    address->sin6_port = htons(remote_base_port);

  auto socket = rtppeer::MIDI_PORT == port ? midi_socket : control_socket;

  // DEBUG("Send to {}, {}, family {} {}. {} {}", port, socket, AF_INET6, address->sin6_family, inet_ntoa(address->sin6_addr), htons(address->sin6_port));

  auto res = ::sendto(
    socket, pb.start, pb.capacity(),
    MSG_CONFIRM, (const struct sockaddr *)address, sizeof(struct sockaddr_in6)
  );

  if (res < 0 || static_cast<uint32_t>(res) != pb.capacity()){
    throw exception(
      "Could not send all data. Sent {}. {}",
      res, strerror(errno)
    );
  }
}


void rtpserver::create_peer_from(parse_buffer_t &buffer, struct sockaddr_in6 *cliaddr, rtppeer::port_e port){
  auto peer = std::make_shared<rtppeer>(name);
  auto address = std::make_shared<struct sockaddr_in6>();
  ::memcpy(address.get(), cliaddr, sizeof(struct sockaddr_in6));
  auto remote_base_port = htons(cliaddr->sin6_port);
  // DEBUG("Address family {} {}. From {}", cliaddr.sin6_family, address->sin6_family, socket);

  // This is the send to the proper ports
  peer->send_event.connect([this, address, remote_base_port](const parse_buffer_t &buff, rtppeer::port_e port){
    this->sendto(buff, port, address.get(), remote_base_port);
  });

  peer->data_ready(buffer, port);

  // After read the first packet I know the initiator_id
  initiator_to_peer[peer->initiator_id] = peer;

  // Setup some callbacks
  auto wpeer = std::weak_ptr(peer);
  peer->connected_event.connect([this, wpeer](const std::string &name, rtppeer::status_e st){
    if (st != rtppeer::CONNECTED)
      return;
    if (wpeer.expired())
      return;
    auto peer = wpeer.lock();
    connected_event(peer);
  });

  peer->midi_event.connect([this](parse_buffer_t &data){
    // DEBUG("Got MIDI from the remote peer into this server.");
    midi_event(data);
  });
}

void rtpserver::send_midi_to_all_peers(parse_buffer_t &buffer){
  for(auto &speers: ssrc_to_peer){
    speers.second->send_midi(buffer);
  }
}
