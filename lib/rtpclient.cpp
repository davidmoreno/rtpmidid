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

#include "rtpmidid/exceptions.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <rtpmidid/iobytes.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/utils.hpp>

using namespace std::chrono_literals;
using namespace rtpmidid;

rtpclient_t::rtpclient_t(std::string name) : peer(std::move(name)) {
  local_base_port = 0;
  remote_base_port = -1; // Not defined
  control_socket = -1;
  control_addr = {0};
  midi_addr = {0};
  timerstate = 0;
  midi_socket = -1;
  peer.initiator_id = ::rtpmidid::rand_u32();
  send_connection = peer.send_event.connect(
      [this](const io_bytes_reader &data, rtppeer_t::port_e port) {
        this->sendto(data, port);
      });
}

rtpclient_t::~rtpclient_t() {
  if (peer.is_connected()) {
    try {
      peer.send_goodbye(rtppeer_t::CONTROL_PORT);
      peer.send_goodbye(rtppeer_t::MIDI_PORT);
    } catch (network_exception &e) {
      ERROR("Removing client without sending proper goodbye. If reconnected "
            "may misbehave.");
    }
  }

  if (control_socket > 0) {
    control_poller.stop();
    close(control_socket);
  }
  if (midi_socket > 0) {
    midi_poller.stop();
    close(midi_socket);
  }
}

void rtpclient_t::connect_to(const std::string &address,
                             const std::string &port) {
  struct addrinfo hints;
  struct addrinfo *sockaddress_list = nullptr;
  char host[NI_MAXHOST], service[NI_MAXSERV];
  socklen_t peer_addr_len = NI_MAXHOST;

  control_socket = midi_socket = -1;

  DEBUG("Try connect to service at {}:{}", address, port);

  try {
    int res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    res = getaddrinfo(address.c_str(), port.c_str(), &hints, &sockaddress_list);
    if (res < 0) {
      DEBUG("Error resolving address {}:{}", address, port);
      throw rtpmidid::exception("Can not resolve address {}:{}. {}", address,
                                port, strerror(errno));
    }
    // Get addr info may return several options, try them in order.
    // we asusme that if the ocntrol success to be created the midi will too.
    auto serveraddr = sockaddress_list;
    for (; serveraddr != nullptr; serveraddr = serveraddr->ai_next) {
      host[0] = service[0] = 0x00;
      getnameinfo(serveraddr->ai_addr, peer_addr_len, host, NI_MAXHOST, service,
                  NI_MAXSERV, NI_NUMERICSERV);
      DEBUG("Try connect to resolved name: {}:{}", host, service);
      // remote_base_port = service;

      control_socket = socket(serveraddr->ai_family, serveraddr->ai_socktype,
                              serveraddr->ai_protocol);
      if (control_socket < 0) {
        continue;
      }
      if (connect(control_socket, serveraddr->ai_addr,
                  serveraddr->ai_addrlen) == 0) {
        break;
      }
      close(control_socket);
    }
    if (!serveraddr) {
      DEBUG("Error opening control socket, port {}", port);
      control_socket = -1;
      throw rtpmidid::exception(
          "Can not open remote rtpmidi control socket. {}", strerror(errno));
    }
    DEBUG("Connected to resolved name: {}:{}", host, service);
    memcpy(&control_addr, serveraddr->ai_addr, sizeof(control_addr));

    struct sockaddr_in6 servaddr;
    socklen_t len = sizeof(servaddr);
    ::getsockname(control_socket, (struct sockaddr *)&servaddr, &len);
    local_base_port = ntohs(servaddr.sin6_port);

    DEBUG("Control port, local: {}, remote at {}:{}", local_base_port, host,
          service);

    control_poller = poller.add_fd_in(control_socket, [this](int) {
      this->data_ready(rtppeer_t::CONTROL_PORT);
    });

    midi_socket = socket(serveraddr->ai_family, serveraddr->ai_socktype,
                         serveraddr->ai_protocol);
    if (midi_socket < 0) {
      throw rtpmidid::exception("Can not open MIDI socket. Out of sockets?");
    }
    // Reuse servaddr, just on next port
    remote_base_port = ntohs(((sockaddr_in *)serveraddr->ai_addr)->sin_port);
    ((sockaddr_in *)serveraddr->ai_addr)->sin_port =
        htons(remote_base_port + 1);

    servaddr.sin6_port = htons(local_base_port + 1);
    auto ret = bind(midi_socket, (struct sockaddr *)&servaddr, len);
    if (ret < 0) {
      throw rtpmidid::exception("Could not bind to local port");
    }

    if (connect(midi_socket, serveraddr->ai_addr, serveraddr->ai_addrlen) < 0) {
      DEBUG("Error opening midi socket, port {}", port);
      throw rtpmidid::exception("Can not open remote rtpmidi MIDI socket. {}",
                                strerror(errno));
    }
    memcpy(&midi_addr, serveraddr->ai_addr, sizeof(midi_addr));
    ::getsockname(midi_socket, (struct sockaddr *)&servaddr, &len);
    auto midi_port = htons(servaddr.sin6_port);
    DEBUG("MIDI PORT at port {}", midi_port);

    midi_poller = poller.add_fd_in(
        midi_socket, [this](int) { this->data_ready(rtppeer_t::MIDI_PORT); });
  } catch (const std::exception &excp) {
    ERROR("Error creating rtp client: {}", excp.what());
    if (control_socket >= 0) {
      control_poller.stop();
      ::close(control_socket);
      control_socket = -1;
    }
    if (midi_socket >= 0) {
      midi_poller.stop();
      ::close(midi_socket);
      midi_socket = -1;
    }
    if (sockaddress_list) {
      freeaddrinfo(sockaddress_list);
    }
    peer.disconnect_event(rtppeer_t::disconnect_reason_e::CANT_CONNECT);
    return;
  }
  if (sockaddress_list) {
    freeaddrinfo(sockaddress_list);
  }

  DEBUG("Connecting midi port {} to {}:{}", local_base_port + 1, address,
        remote_base_port + 1);

  // If not connected, connect now the MIDI port
  connected_connection = peer.connected_event.connect(
      [this, address, port](const std::string &name,
                            rtppeer_t::status_e status) {
        if (status == rtppeer_t::CONTROL_CONNECTED) {
          DEBUG("Connected midi port {} to {}:{}", local_base_port + 1, address,
                remote_base_port + 1);
          peer.connect_to(rtppeer_t::MIDI_PORT);
        } else if (status == rtppeer_t::CONNECTED) {
          connected();
        }
      });

  peer.connect_to(rtppeer_t::CONTROL_PORT);

  connect_timer = poller.add_timer_event(5s, [this] {
    // ce will be scope removed at end or destruction of lambda
    peer.disconnect_event(rtppeer_t::CONNECT_TIMEOUT);
    connected_connection.disconnect();
  });
}

/**
 * Send the periodic latency and connection checks
 *
 * At first six times as received confirmation from other end. Then Every 10
 * secs. Check connected() function for actuall recall code.
 *
 * This just checks timeout and sends the ck.
 */
void rtpclient_t::connected() {
  connect_timer.disable();

  ck_connection = peer.ck_event.connect([this](float ms) {
    ck_timeout.disable();
    if (timerstate < 6) {
      timer_ck =
          poller.add_timer_event(1500ms, [this] { send_ck0_with_timeout(); });
      timerstate++;
    } else {
      timer_ck =
          poller.add_timer_event(10s, [this] { send_ck0_with_timeout(); });
    }
  });
  send_ck0_with_timeout();
}

void rtpclient_t::send_ck0_with_timeout() {
  peer.send_ck0();
  ck_timeout = poller.add_timer_event(5s, [this] {
    peer.disconnect_event(rtppeer_t::disconnect_reason_e::CK_TIMEOUT);
  });
}

void rtpclient_t::sendto(const io_bytes &pb, rtppeer_t::port_e port) {
  auto peer_addr = (port == rtppeer_t::MIDI_PORT) ? midi_addr : control_addr;

  auto socket = rtppeer_t::MIDI_PORT == port ? midi_socket : control_socket;

  for (;;) {
    ssize_t res =
        ::sendto(socket, pb.start, pb.size(), MSG_CONFIRM,
                 (const struct sockaddr *)&peer_addr, sizeof(peer_addr));

    if (static_cast<uint32_t>(res) == pb.size())
      break;

    if (res == -1) {
      if (errno == EINTR) {
        DEBUG("Retry sendto because of EINTR");
        continue;
      }

      ERROR("Client: Could not send all data to {}:{}. Sent {}. {} ({})",
            peer.remote_name, remote_base_port, res, strerror(errno), errno);
      throw network_exception(errno);
    }

    DEBUG("Could not send whole message: only {} of {}", res, pb.size());
    break;
  }
}

void rtpclient_t::reset() {
  remote_base_port = 0;
  peer.reset();
}

void rtpclient_t::data_ready(rtppeer_t::port_e port) {
  uint8_t raw[1500];
  struct sockaddr_in6 cliaddr;
  unsigned int len = sizeof(cliaddr);
  auto socket = port == rtppeer_t::CONTROL_PORT ? control_socket : midi_socket;
  auto n = recvfrom(socket, raw, 1500, MSG_DONTWAIT,
                    (struct sockaddr *)&cliaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0) {
    throw exception("Error reading from rtppeer {}:{}", peer.remote_name,
                    remote_base_port);
  }

  auto buffer = io_bytes_reader(raw, n);
  peer.data_ready(std::move(buffer), port);
}
