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

#include "rtpmidid/exceptions.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <optional>
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
#include <rtpmidid/network.hpp>
#include <rtpmidid/networkaddress.hpp>
#include <rtpmidid/poller.hpp>
#include <rtpmidid/rtpclient.hpp>
#include <rtpmidid/udppeer.hpp>
#include <rtpmidid/utils.hpp>

using namespace std::chrono_literals;
using namespace rtpmidid;

/**
 * @short Create the rtp client
 *
 * @param name Name of the client.
 *
 * This client will try to connect to the list of `connect_to` addresses:ports,
 * one by one.
 *
 * On each call to `connect_next` it will try to connect to the next address.
 *
 * Each connection has some internal state, as resolve dns, connect control,
 * connect midi and finally connected. But each one of these may fail an then
 * try to connect the next address.
 *
 * The flow is as follows:
 *
 */
rtpclient_t::rtpclient_t(std::string name) : peer(std::move(name)) {
  peer.initiator_id = ::rtpmidid::rand_u32();
  send_connection = peer.send_event.connect(
      [this](const io_bytes_reader &data, rtppeer_t::port_e port) {
        try {
          this->sendto(data, port);
        } catch (const network_exception &e) {
          ERROR("Error sending data to {}:{}. {}", peer.remote_name,
                remote_base_port, e.what());
          peer.disconnect_event(rtppeer_t::disconnect_reason_e::NETWORK_ERROR);
        }
      });

  peer_disconnect_event_connection = peer.disconnect_event.connect(
      [this](rtppeer_t::disconnect_reason_e reason) {
        if (reason == rtppeer_t::disconnect_reason_e::CONNECT_TIMEOUT ||
            reason == rtppeer_t::disconnect_reason_e::CANT_CONNECT) {
          INFO("Could not connect, try next peer. Reason: {}", reason);
          connect_to_next();
        } else {
          INFO("Disconnected reason: {}. Not trying to connect again.", reason);
        }
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

bool rtpclient_t::connect_to(const std::vector<endpoint_t> &address_port) {
  bool list_was_empty = address_port_pending.empty();
  for (auto &endpoint : address_port) {
    DEBUG("Add {} to pending list", endpoint);
    address_port_pending.push_back(endpoint);
  }
  DEBUG("All ports to try to connect to in order: {}", address_port_pending);

  if (list_was_empty) {
    INFO("Start connect to remote peer. {} endpoints available to try.",
         address_port_pending.size());
    return connect_to_next();
  }
  return false;
}

bool rtpclient_t::connect_to_next() {
  if (address_port_pending.empty()) {
    ERROR("Could not find any valid remote address to connect to");
    connect_timer.disable();
    connected_event("", rtppeer_t::NOT_CONNECTED);
    return false;
  }
  auto endpoint = address_port_pending.front();
  address_port_pending.pop_front();
  return connect_to(endpoint.hostname, endpoint.port);
}

// For each available adress on the hostname:port combo, call callback
// If callback returns true, stop iterating.
bool for_each_address(const std::string &hostname, const std::string &port,
                      std::function<bool(addrinfo *serveraddr)> callback) {
  struct addrinfo hints {};
  struct addrinfo *sockaddress_list = nullptr;

  int res = 0;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  res = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &sockaddress_list);
  if (res < 0) {
    DEBUG("Error resolving address {}:{}", hostname, port);
    return false;
  }
  // Get addr info may return several options, try them in order.
  // we asusme that if the control success to be created the midi will too.
  addrinfo *serveraddr = sockaddress_list;
  for (; serveraddr != nullptr; serveraddr = serveraddr->ai_next) {
    bool stop = callback(serveraddr);
    if (stop) {
      DEBUG("Stop iterating addresses");
      freeaddrinfo(sockaddress_list);
      return true;
    }
  }
  freeaddrinfo(sockaddress_list);
  // Could not connect to any address
  return false;
}

struct socket_port_sockaddr_t {
  int socket;
  int port;
  sockaddr_storage addr;
};

struct control_midi_ports_t {
  std::string remote_address;
  int remote_base_port;
  socket_port_sockaddr_t control;
  socket_port_sockaddr_t midi;
};

/**
 * Connect to a remote host and port
 *
 * @param local_base_port if 0, any available port will be used
 * @param serveraddr
 * @param port_type
 *
 * @return control and midi ports information struct
 *
 * Create the socket and connect it. If connection is sucessfull,
 * return the data. Else return an empty optional.
 */
std::optional<socket_port_sockaddr_t> connect_udp_port(int local_base_port,
                                                       addrinfo *serveraddr) {
  struct sockaddr_in6 myaddr {};
  memset(&myaddr, 0, sizeof(myaddr));
  socklen_t len = sizeof(myaddr);
  auto socketfd = socket(serveraddr->ai_family, serveraddr->ai_socktype,
                         serveraddr->ai_protocol);
  if (socketfd < 0) {
    DEBUG("Could not create socket");
    return std::nullopt;
  }

  myaddr.sin6_port = htons(local_base_port);
  // NOLINTNEXTLINE
  auto ret = bind(socketfd, (struct sockaddr *)&myaddr, len);
  if (ret < 0) {
    DEBUG("Could not bind socket at UDP port {}", local_base_port);
    close(socketfd);
    return std::nullopt;
  }

  if (connect(socketfd, serveraddr->ai_addr, serveraddr->ai_addrlen) < 0) {
    DEBUG("Could not connect socket");
    close(socketfd);
    return std::nullopt;
  }

  // NOLINTNEXTLINE
  ::getsockname(socketfd, (struct sockaddr *)&myaddr, &len);
  auto udp_port = htons(myaddr.sin6_port);
  DEBUG("PORT at port {}, socket {}", udp_port, socketfd);

  socket_port_sockaddr_t retv{
      .socket = socketfd,
      .port = udp_port,
  };

  memcpy(&retv.addr, serveraddr->ai_addr, serveraddr->ai_addrlen);
  return retv;
}

/**
 * Connect to a remote host and port
 *
 * @param local_base_port if 0, any available port will be used
 * @param hostname
 * @param port
 *
 * @return control and midi ports information struct
 *
 * For each possible address (a name can have many posibilities),
 * try to connect to each to both the control and midi ports.
 *
 * If both succeed, return them. Else the return is an empty optional.
 */
std::optional<control_midi_ports_t>
connect_control_and_midi_sockets(int local_base_port,
                                 const std::string &hostname,
                                 const std::string &portname) {
  std::optional<control_midi_ports_t> ret = std::nullopt;

  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-type-cstyle-cast)
  for_each_address(hostname, portname, [&](addrinfo *serveraddr) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    host[0] = port[0] = '\0';
    getnameinfo(serveraddr->ai_addr, serveraddr->ai_addrlen, host, NI_MAXHOST,
                port, NI_MAXSERV, NI_NUMERICSERV);

    DEBUG("Try to connect to address: {}:{} ({}:{})", host, port, hostname,
          portname);

    auto control = connect_udp_port(local_base_port, serveraddr);
    if (!control) {
      DEBUG("Could not connect {}:{} to control port", hostname, portname);
      return false;
    }

    // advance the port by 1, so it points to midi
    auto base_port = ntohs(((sockaddr_in *)serveraddr->ai_addr)->sin_port);
    ((sockaddr_in *)serveraddr->ai_addr)->sin_port = htons(base_port + 1);

    auto midi = connect_udp_port(control->port + 1, serveraddr);
    if (!midi) {
      DEBUG("Could not connect to MIDI port");
      close(control->socket);
      return false;
    }
    ret = control_midi_ports_t{
        .remote_address = host,
        .remote_base_port = base_port,
        .control = *control,
        .midi = *midi,
    };

    return true;
  });
  // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-type-cstyle-cast)
  return ret;
}

/**
 * Connect to a remote host and port
 *
 * @param address
 * @param port
 *
 * @return true if connection dance started
 *
 * It gets apair of valid sockets, addrinfo and ports for both control and midi
 * stores the data and starts the connection dance with the try to connect to
 * control.
 */
bool rtpclient_t::connect_to(const std::string &address,
                             const std::string &port) {

  auto ports = connect_control_and_midi_sockets(0, address, port);
  if (!ports) {
    DEBUG("Error opening control and midi sockets, port {}:{}", address, port);
    peer.disconnect_event(rtppeer_t::disconnect_reason_e::CANT_CONNECT);
    return false;
  }
  DEBUG("Got UDP local ports: {} and {}, connection to {}:{} and {}:{}",
        ports->control.port, ports->midi.port, address, ports->remote_base_port,
        address, ports->remote_base_port + 1);

  control_socket = ports->control.socket;
  local_base_port = ports->control.port;
  midi_socket = ports->midi.socket;
  peer.remote_name = ports->remote_address;
  peer.remote_base_port = ports->remote_base_port;
  remote_base_port = ports->remote_base_port;

  DEBUG("CONTROL at {}, remote base at {}", ports->control.addr,
        remote_base_port);
  DEBUG("MIDI at {}", ports->midi.addr);

  memcpy(&midi_addr, &ports->midi.addr, sizeof(sockaddr_storage));
  memcpy(&control_addr, &ports->control.addr, sizeof(sockaddr_storage));

  DEBUG("CONTROL at {}", control_addr);
  DEBUG("MIDI at {}", midi_addr);

  control_poller = poller.add_fd_in(control_socket, [this](int) {
    this->data_ready(rtppeer_t::CONTROL_PORT);
  });

  midi_poller = poller.add_fd_in(
      midi_socket, [this](int) { this->data_ready(rtppeer_t::MIDI_PORT); });

  // If connected, connect now the MIDI port
  connected_connection = peer.connected_event.connect(
      [this, address, port](const std::string &name,
                            rtppeer_t::status_e status) {
        if (status == rtppeer_t::CONTROL_CONNECTED) {
          DEBUG("Connected midi port {} to {}:{}", local_base_port + 1, address,
                remote_base_port + 1);
          peer.connect_to(rtppeer_t::MIDI_PORT);
        } else if (status == rtppeer_t::CONNECTED) {
          connected_event(name, status);
          connected();
        }
      });

  // We start to try to connect to the control port
  // the internal state machine will try after connecting here connect
  // to the midi port
  DEBUG("Connecting control port {} to {}:{}", local_base_port, address,
        remote_base_port);

  connect_timer = poller.add_timer_event(5s, [this] {
    DEBUG("Timeout connecting to {}:{}, status {}", peer.remote_name,
          remote_base_port, peer.status);
    if (peer.status != rtppeer_t::CONNECTED)
      peer.disconnect();
    peer.disconnect_event(rtppeer_t::CONNECT_TIMEOUT);
    // connected_connection.disconnect();
  });

  peer.connect_to(rtppeer_t::CONTROL_PORT);

  return true;
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
  sockaddr_storage *peer_addr = nullptr;
  int socket = -1;

  if (port == rtppeer_t::MIDI_PORT) {
    socket = midi_socket;
    peer_addr = &midi_addr;
  } else if (port == rtppeer_t::CONTROL_PORT) {
    socket = control_socket;
    peer_addr = &control_addr;
  } else {
    throw exception("Unknown port {}", port);
  }

  // DEBUG("Send {} bytes to {} {}, socket {}", port, pb.size(), *peer_addr,
  //       socket);

  // NOLINTNEXTLINE
  sockaddr *peer_addr_sockaddr = reinterpret_cast<sockaddr *>(peer_addr);

  for (;;) {
    ssize_t res = ::sendto(socket, pb.start, pb.size(), 0, peer_addr_sockaddr,
                           sizeof(sockaddr_storage));

    if (static_cast<uint32_t>(res) == pb.size())
      break;

    if (res == -1) {
      if (errno == EINTR) {
        DEBUG("Retry sendto because of EINTR");
        continue;
      }

      ERROR("Client: Could not send all data to {}. Sent {}. {} ({})",
            *peer_addr, res, strerror(errno), errno);
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

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
void rtpclient_t::data_ready(rtppeer_t::port_e port) {
  uint8_t raw[1500];
  struct sockaddr_storage cliaddr {};
  unsigned int len = sizeof(cliaddr);
  auto socket = port == rtppeer_t::CONTROL_PORT ? control_socket : midi_socket;
  // NOLINTNEXTLINE
  auto cliaddr_sockaddr = reinterpret_cast<sockaddr *>(&cliaddr);
  auto n = recvfrom(socket, raw, 1500, MSG_DONTWAIT, cliaddr_sockaddr, &len);
  // DEBUG("Got some data from control: {}", n);
  if (n < 0) {
    throw exception("Error reading from rtppeer {}:{}", peer.remote_name,
                    remote_base_port);
  }

  auto buffer = io_bytes_reader(raw, n);
  peer.data_ready(std::move(buffer), port);
}
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

// Make the order follow as written at statemachines.md, so can
// filled with simple copy/paste and multicursors
struct state_change_t {
  rtpclient_t::states_e state;
  rtpclient_t::states_e next_state;
  rtpclient_t::event_e event;
};

state_change_t state_changes[] = {
    {rtpclient_t::WaitToStart, rtpclient_t::PrepareNextDNS,
     rtpclient_t::Started},
    {rtpclient_t::PrepareNextDNS, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::NextReady},
    {rtpclient_t::PrepareNextDNS, rtpclient_t::ErrorCantConnect,
     rtpclient_t::ResolveListExhausted},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::PrepareNextDNS,
     rtpclient_t::ConnectListExhausted},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ResolveFailed},
    {rtpclient_t::ResolveNextIpPort, rtpclient_t::ConnectControl,
     rtpclient_t::Resolved},
    {rtpclient_t::ConnectControl, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ConnectFailed},
    {rtpclient_t::ConnectControl, rtpclient_t::ConnectMidi,
     rtpclient_t::Connected},
    {rtpclient_t::ConnectMidi, rtpclient_t::AllConnected,
     rtpclient_t::Connected},
    {rtpclient_t::ConnectMidi, rtpclient_t::DisconnectControl,
     rtpclient_t::ConnectFailed},
    {rtpclient_t::DisconnectControl, rtpclient_t::ResolveNextIpPort,
     rtpclient_t::ConnectFailed},
};

void rtpclient_t::state_machine(rtpclient_t::event_e event) {
  // The state machine itself
  rtpclient_t::states_e next_state =
      ErrorCantConnect; // default state, if not changed
  for (auto &change : state_changes) {
    if (state == change.state && event == change.event) {
      next_state = change.next_state;
      break;
    }
  }
  INFO("State machine: {} -[{}]-> {}", state, event, next_state);

  state = next_state;

  // Call the new state functions
  switch (state) {
  case WaitToStart:
    break;
  case PrepareNextDNS:
    resolve_next_dns();
    break;
  case ResolveNextIpPort:
    connect_next_ip_port();
    break;
  case ConnectControl:
    connect_control();
    break;
  case ConnectMidi:
    connect_midi();
    break;
  case AllConnected:
    connected_();
    break;
  case ErrorCantConnect:
    error_cant_connect();
    break;
  case DisconnectControl:
    disconnect_control();
    break;
  }
}

void rtpclient_t::resolve_next_dns() {
  if (address_port_pending.empty()) {
    state_machine(ConnectListExhausted);
    return;
  }
  resolve_next_dns_endpoint = address_port_pending.back();
  address_port_pending.pop_back();

  state_machine(NextReady);
}

void rtpclient_t::connect_next_ip_port() {
  // First time, get next address and resolve
  if (!resolve_next_dns_sockaddress_list) {
    struct addrinfo hints {};
    int res = 0;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    res = getaddrinfo(resolve_next_dns_endpoint.hostname.c_str(),
                      resolve_next_dns_endpoint.port.c_str(), &hints,
                      &resolve_next_dns_sockaddress_list);
    if (res < 0) {
      state_machine(ResolveFailed);
    }

    resolve_next_dns_sockaddress = resolve_next_dns_sockaddress_list;
  } else {
    // Following use the next resolved item
    resolve_next_dns_sockaddress = resolve_next_dns_sockaddress->ai_next;
  }

  // If any left, go for it, if not failed resolve this address port pair
  if (resolve_next_dns_sockaddress) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    host[0] = port[0] = '\0';
    getnameinfo(resolve_next_dns_sockaddress->ai_addr,
                resolve_next_dns_sockaddress->ai_addrlen, host, NI_MAXHOST,
                port, NI_MAXSERV, NI_NUMERICSERV);

    DEBUG("Try to connect to address: {}:{}", host, port);

    state_machine(Resolved);
  } else {
    freeaddrinfo(resolve_next_dns_sockaddress_list);
    resolve_next_dns_sockaddress_list = nullptr;
    state_machine(ConnectListExhausted);
  }
}

void rtpclient_t::connect_control() {
  control_peer = std::move(
      udppeer_t(network_address_t(resolve_next_dns_sockaddress->ai_addr,
                                  resolve_next_dns_sockaddress->ai_addrlen)));
  if (!control_peer.is_open()) {
    DEBUG("Could not connect {}:{} to control port",
          resolve_next_dns_endpoint.hostname, resolve_next_dns_endpoint.port);
    state_machine(ConnectFailed);
    return;
  }
  control_connected_event_connection = peer.connected_event.connect(
      [this](const std::string &name, rtppeer_t::status_e status) {
        if (status == rtppeer_t::CONTROL_CONNECTED) {
          auto address = control_peer.get_address();
          DEBUG("Connected control port {} to {}:{}", address.port(),
                address.hostname(), address.port());
          control_connected_event_connection.disconnect();
          state_machine(Connected);
        } else {
          state_machine(ConnectFailed);
        }
      });
}

void rtpclient_t::connect_midi() {
  // advance the port by 1, so it points to midi
  auto base_port =
      ntohs(((sockaddr_in *)resolve_next_dns_sockaddress->ai_addr)->sin_port);
  ((sockaddr_in *)resolve_next_dns_sockaddress->ai_addr)->sin_port =
      htons(base_port + 1);

  auto midi =
      connect_udp_port(local_base_port + 1, resolve_next_dns_sockaddress);
  if (!midi) {
    DEBUG("Could not connect {}:{} to midi port",
          resolve_next_dns_endpoint.hostname, resolve_next_dns_endpoint.port);
    state_machine(ConnectFailed);
    return;
  }

  midi_socket = midi->socket;

  // TODO: Actual send the midi connect message, wait for ACK, then connected
  // event

  state_machine(Connected);
}

void rtpclient_t::disconnect_control() {
  // TODO send goodbye to control
  close(control_socket);
  state_machine(ConnectFailed);
}

void rtpclient_t::connected_() { INFO("Connected"); }
void rtpclient_t::error_cant_connect() {
  connected_event("", rtppeer_t::NOT_CONNECTED);
}

void rtpclient_t::add_server_address(const std::string &address,
                                     const std::string &port) {
  address_port_pending.push_back({address, port});
}

void rtpclient_t::connect() { state_machine(Started); }
