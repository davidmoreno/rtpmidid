/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "rtpmidid/rtppeer.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/network.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpserver.hpp>

using namespace rtpmidid;

// NOLINTNEXTLINE(bugprone-swappable-parameters)
rtpserver_t::rtpserver_t(std::string _name, const std::string &port)
    : name(std::move(_name)) {
  struct addrinfo *sockaddress_list = nullptr;

  try {
    struct addrinfo hints {};

    int res = -1;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    const char *cport = (port == "") ? nullptr : port.c_str();

    res = getaddrinfo("::", cport, &hints, &sockaddress_list);
    if (res < 0) {
      DEBUG("Error resolving address {}:{}", "::", port);
      throw rtpmidid::exception("Can not resolve address {}:{}. {}", "::", port,
                                strerror(errno));
    }
    // Get addr info may return several options, try them in order.
    // we asume that if the control success to be created the midi will too.
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXHOST> service{};
    socklen_t peer_addr_len = NI_MAXHOST;
    auto listenaddr = sockaddress_list;
    for (; listenaddr != nullptr; listenaddr = listenaddr->ai_next) {
      host[0] = service[0] = 0x00;
      getnameinfo(listenaddr->ai_addr, peer_addr_len, host.data(), NI_MAXHOST,
                  service.data(), NI_MAXSERV, NI_NUMERICSERV);
      DEBUG("Try listen at {}:{}", host.data(), service.data());

      control_socket = socket(listenaddr->ai_family, listenaddr->ai_socktype,
                              listenaddr->ai_protocol);
      if (control_socket < 0) {
        continue; // Bad socket. Try next.
      }
      if (bind(control_socket, listenaddr->ai_addr, listenaddr->ai_addrlen) ==
          0) {
        break;
      }
      close(control_socket);
      control_socket = -1;
    }
    if (!listenaddr) {
      throw rtpmidid::exception("Can not open rtpmidi control socket. {}.",
                                strerror(errno));
    }
    struct sockaddr_storage addr {};
    unsigned int len = sizeof(addr);
    res = ::getsockname(control_socket, sockaddr_storage_to_sockaddr(&addr),
                        &len);
    if (res < 0) {
      throw rtpmidid::exception("Error getting info the newly created midi "
                                "socket. Can not create server.");
    }
    control_port = sockaddr_storage_get_port(&addr);

    DEBUG("Control port at {}:{}", host.data(), control_port);
    midi_port = control_port + 1;

    control_poller = poller.add_fd_in(control_socket, [this](int) {
      this->data_ready(rtppeer_t::CONTROL_PORT);
    });

    midi_socket = socket(listenaddr->ai_family, listenaddr->ai_socktype,
                         listenaddr->ai_protocol);
    if (midi_socket < 0) {
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    // Reuse listenaddr, just on next port
    // NOLINTNEXTLINE
    ((sockaddr_in *)listenaddr->ai_addr)->sin_port = htons(midi_port);
    if (bind(midi_socket, listenaddr->ai_addr, listenaddr->ai_addrlen) < 0) {
      throw rtpmidid::exception("Can not open MIDI socket. {}.",
                                strerror(errno));
    }
    midi_poller = poller.add_fd_in(
        midi_socket, [this](int) { this->data_ready(rtppeer_t::MIDI_PORT); });
  } catch (const std::exception &e) {
    ERROR("Error creating server at port {}: {}", control_port, e.what());
    if (control_socket != -1) {
      control_poller.stop();
      ::close(control_socket);
      control_socket = -1;
    }
    if (midi_socket != -1) {
      midi_poller.stop();
      ::close(midi_socket);
      midi_socket = -1;
    }
    if (sockaddress_list) {
      freeaddrinfo(sockaddress_list);
    }
    throw;
  }
  if (sockaddress_list) {
    freeaddrinfo(sockaddress_list);
  }

  INFO("Listening RTP MIDI connections at 0.0.0.0:{}, with name: '{}'",
       control_port, name);
}

// NOLINTNEXTLINE(bugprone-exception-escape)
rtpserver_t::~rtpserver_t() {
  DEBUG("~rtpserver_t({})", name);
  // Must clear here, to be able to use the control and midi
  // sockets
  for (auto &peerinfo : peers) {
    if (peerinfo.peer) {
      peerinfo.peer->send_goodbye(rtppeer_t::CONTROL_PORT);
      peerinfo.peer->send_goodbye(rtppeer_t::MIDI_PORT);
    }
  }
  if (control_socket >= 0) {
    control_poller.stop();
    close(control_socket);
  }
  if (midi_socket >= 0) {
    midi_poller.stop();
    close(midi_socket);
  }
}

std::shared_ptr<rtppeer_t>
rtpserver_t::get_peer_by_packet(io_bytes_reader &buffer,
                                rtppeer_t::port_e port) {
  // Commands may be by SSRC or initiator_id
  auto command =
      rtppeer_t::commands_e((uint16_t(buffer.start[2]) << 8) + buffer.start[3]);

  switch (command) {
  case rtppeer_t::IN:
  case rtppeer_t::OK:
  case rtppeer_t::NO: {
    buffer.position = buffer.start + 8;
    auto initiator_id = buffer.read_uint32();
    buffer.position = buffer.start;

    return get_peer_by_initiator_id(initiator_id);
  }
  case rtppeer_t::BY: {
    buffer.position = buffer.start + 12;
    auto ssrc_id = buffer.read_uint32();
    buffer.position = buffer.start;

    return get_peer_by_ssrc(ssrc_id);
  }
  case rtppeer_t::CK:
  case rtppeer_t::RS: {
    buffer.position = buffer.start + 4;
    auto ssrc = buffer.read_uint32();
    buffer.position = buffer.start;
    return get_peer_by_ssrc(ssrc);
  }
  default:
    if (port == rtppeer_t::MIDI_PORT && (buffer.start[1] & 0x7F) == 0x61) {
      buffer.read_uint32();
      buffer.read_uint32();
      auto ssrc = buffer.read_uint32();
      buffer.position = buffer.start;

      return get_peer_by_ssrc(ssrc);
    }
    DEBUG("Unknown COMMAND id {:X} / {:X}", int(command), buffer.start[1]);
    return nullptr;
  }
}

std::shared_ptr<rtppeer_t> rtpserver_t::get_peer_by_ssrc(uint32_t ssrc) {
  for (auto &peerdata : peers) {
    if (peerdata.peer->remote_ssrc == ssrc) {
      return peerdata.peer;
    }
  }
  return nullptr;
}

std::shared_ptr<rtppeer_t>
rtpserver_t::get_peer_by_initiator_id(uint32_t initiator_id) {
  for (auto &peerdata : peers) {
    if (peerdata.peer->initiator_id == initiator_id) {
      return peerdata.peer;
    }
  }
  return nullptr;
}

void rtpserver_t::data_ready(rtppeer_t::port_e port) {
  std::array<uint8_t, 1500> raw{};
  struct sockaddr_storage cliaddr {};
  unsigned int len = sizeof(cliaddr);
  auto socket =
      (port == rtppeer_t::CONTROL_PORT) ? control_socket : midi_socket;
  auto n = recvfrom(socket, raw.data(), 1500, MSG_DONTWAIT,
                    sockaddr_storage_to_sockaddr(&cliaddr), &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0) {
    auto netport = (port == rtppeer_t::CONTROL_PORT) ? control_port : midi_port;
    throw exception("Error reading from server 0.0.0.0:{}", netport);
  }

  auto buffer = io_bytes_reader(raw.data(), n);

  auto peer = get_peer_by_packet(buffer, port);
  if (peer) {
    peer->data_ready(std::move(buffer), port);
  } else {
    // If I dont know the other peer I'm only interested in IN, ignore others
    // If it is not a CONTROL PORT the messages come in the wrong order. The
    // first IN should create the peer.
    if (rtppeer_t::is_command(buffer) && buffer.start[2] == 'I' &&
        buffer.start[3] == 'N') {
      create_peer_from(std::move(buffer), &cliaddr, port);
    } else {
      std::array<char, NI_MAXHOST> host{};
      std::array<char, NI_MAXSERV> service{};

      getnameinfo(sockaddr_storage_to_sockaddr(&cliaddr), len, host.data(),
                  NI_MAXHOST, service.data(), NI_MAXSERV, NI_NUMERICSERV);

      DEBUG(
          "Unknown peer ({}/{}), and not connect on control. Ignoring {} port.",
          host.data(), service.data(),
          port == rtppeer_t::MIDI_PORT ? "MIDI" : "Control");

      buffer.print_hex(true);
    }
  }
}

void rtpserver_t::sendto(const io_bytes_reader &pb, rtppeer_t::port_e port,
                         struct sockaddr_storage *address,
                         int remote_base_port) {
  if (port == rtppeer_t::MIDI_PORT)
    sockaddr_storage_set_port(address, remote_base_port + 1);
  else
    sockaddr_storage_set_port(address, remote_base_port);

  auto socket = rtppeer_t::MIDI_PORT == port ? midi_socket : control_socket;

  // DEBUG("Send to {}, {}, family {} {}. {} {}", port, socket, AF_INET6,
  // address->sin6_family, inet_ntoa(address->sin6_addr),
  // htons(address->sin6_port));

  for (;;) {
    ssize_t res = ::sendto(socket, pb.start, pb.size(), MSG_CONFIRM,
                           sockaddr_storage_to_sockaddr(address),
                           sizeof(sockaddr_storage));

    if (static_cast<uint32_t>(res) == pb.size())
      break;

    std::array<char, INET6_ADDRSTRLEN> addr_buffer{};
    inet_ntop(AF_INET6, address, addr_buffer.data(), INET6_ADDRSTRLEN);

    if (res == -1) {
      if (errno == EINTR) {
        DEBUG("Retry sendto because of EINTR");
        continue;
      }

      ERROR("Server: Could not send all data to {}: {}", addr_buffer.data(),
            strerror(errno));
      throw network_exception(errno);
    }

    DEBUG("Could not send whole message to {}: only {} of {}",
          addr_buffer.data(), res, pb.size());
    break;
  }
}

void rtpserver_t::create_peer_from(io_bytes_reader &&buffer,
                                   struct sockaddr_storage *cliaddr,
                                   rtppeer_t::port_e port) {

  auto peer = std::make_shared<rtppeer_t>(name);
  auto address = std::make_shared<struct sockaddr_storage>();
  ::memcpy(address.get(), cliaddr, sizeof(struct sockaddr_storage));
  auto remote_base_port = sockaddr_storage_get_port(cliaddr);

  std::array<char, INET6_ADDRSTRLEN> astring{};
  inet_ntop(AF_INET6, sockaddr_storage_get_addr_in6(address.get()),
            astring.data(), INET6_ADDRSTRLEN);
  DEBUG("Connected from {}:{}", astring.data(), remote_base_port);
  peer->remote_address = astring.data();
  peer->remote_base_port = remote_base_port;

  auto &peerdata = peers.emplace_back();
  peerdata.peer = peer;
  peer->remote_address = astring.data();
  peer->remote_base_port = remote_base_port;

  // peer_data_t peerdata;

  // This is the send to the proper ports
  peerdata.send_event_connection = peer->send_event.connect(
      [this, address, remote_base_port](const io_bytes_reader &buff,
                                        rtppeer_t::port_e port) {
        this->sendto(buff, port, address.get(), remote_base_port);
      });

  // After read the first packet I know the initiator_id and ssrc

  // Setup some callbacks
  auto wpeer = std::weak_ptr(peer);
  peerdata.connected_event_connection = peer->connected_event.connect(
      [this, wpeer](const std::string &name, rtppeer_t::status_e st) {
        if (wpeer.expired())
          return;
        auto peer = wpeer.lock();

        auto peerdata = std::find_if(peers.begin(), peers.end(),
                                     [&peer](const peer_data_t &datapeer) {
                                       return datapeer.peer == peer;
                                     });
        peerdata->timer_connection.disable();
        peerdata->timer_ck_connection =
            peer->ck_event.connect([this, wpeer](float) {
              if (wpeer.expired())
                return;
              auto peer = wpeer.lock();
              rearm_ck_timeout(peer);
            });

        rearm_ck_timeout(peer);

        if (st != rtppeer_t::CONNECTED)
          return;
        connected_event(peer);
      });

  peerdata.midi_event_connection =
      peer->midi_event.connect([this](const io_bytes_reader &data) {
        // DEBUG("Got MIDI from the remote peer into this server.");
        midi_event(data);
      });

  peerdata.disconnect_event_connection = peer->disconnect_event.connect(
      [this, wpeer](rtpmidid::rtppeer_t::disconnect_reason_e dr) {
        if (wpeer.expired())
          return;
        auto peer = wpeer.lock();
        peers.erase(        //
            std::remove_if( //
                peers.begin(), peers.end(),
                [&peer](const peer_data_t &datapeer) {
                  return datapeer.peer == peer;
                }),
            peers.end());
        //                  this->initiator_to_peer.erase(peer->initiator_id);
        // this->ssrc_to_peer.erase(peer->remote_ssrc);
      });

  // And a timeout to remove the peer if it does not connect the midi port soon
  peerdata.timer_connection =
      poller.add_timer_event(std::chrono::seconds(5), [wpeer]() {
        if (wpeer.expired())
          return;
        auto peer = wpeer.lock();
        if (peer->status == rtppeer_t::status_e::CONTROL_CONNECTED) {
          DEBUG("Timeout waiting for MIDI connection. Disconnecting.");
          peer->disconnect();
        }
      });

  // Finally pass the data to the peer
  peer->data_ready(std::move(buffer), port);
}

void rtpserver_t::send_midi_to_all_peers(const io_bytes_reader &buffer) {
  for (auto &speers : peers) {
    speers.peer->send_midi(buffer);
  }
}

void rtpserver_t::rearm_ck_timeout(std::shared_ptr<rtppeer_t> peer) {
  auto peerdata = std::find_if(
      peers.begin(), peers.end(),
      [&peer](const peer_data_t &datapeer) { return datapeer.peer == peer; });
  if (peerdata == peers.end())
    return;
  peerdata->timer_connection.disable();

  // If no signal in 60 secs, remove the peer
  peerdata->timer_connection =
      poller.add_timer_event(std::chrono::seconds(60), [peerdata, peer]() {
        DEBUG("Timeout waiting for CK. Disconnecting.");
        peer->disconnect();
      });
}