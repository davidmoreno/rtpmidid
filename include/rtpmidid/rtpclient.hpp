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

#pragma once

#include "./iobytes.hpp"
#include "./poller.hpp"
#include "./rtppeer.hpp"
#include "./signal.hpp"
#include <list>
#include <string>

extern "C" {
struct addrinfo;
}

namespace rtpmidid {
struct address_port_t {
  std::string address;
  std::string port;
};

/**
 * @short A RTP Client
 *
 * It connects to a remote address and port, do all the connection parts,
 * and emits midi events or on_disconnect if not valid.
 */
class rtpclient_t {
  NON_COPYABLE_NOR_MOVABLE(rtpclient_t)
public:
  rtppeer_t peer;
  // signal_t<> connect_failed_event;
  poller_t::timer_t connect_timer;
  poller_t::timer_t ck_timeout;
  int connect_count =
      3; // how many times we tried to connect, after 3, final fail.

  int control_socket = -1;
  int midi_socket = -1;
  struct sockaddr_storage control_addr {};
  struct sockaddr_storage midi_addr {};

  uint16_t local_base_port = 0;
  uint16_t remote_base_port = -1;
  poller_t::timer_t timer_ck;
  /// A simple state machine. We need to send 6 CK one after another, and then
  /// every 10 secs.
  uint8_t timerstate = 0;
  connection_t<const io_bytes_reader &, rtppeer_t::port_e> send_connection;
  connection_t<float> ck_connection;
  connection_t<const std::string &, rtppeer_t::status_e> connected_connection;
  connection_t<rtppeer_t::disconnect_reason_e> peer_disconnect_event_connection;
  connection_t<const std::string &, rtppeer_t::status_e>
      peer_connected_event_connection;

  poller_t::listener_t midi_poller;
  poller_t::listener_t control_poller;
  signal_t<const std::string &, rtppeer_t::status_e> connected_event;

  /// The states fot eh state machine. They correspond directly to functions
  /// with same name
  enum states_e {
    WaitToStart,
    PrepareNextDNS,
    ConnectNextIpPort,
    ConnectControl,
    ConnectMidi,
    DisconnectControl,
    AllConnected,
    ErrorCantConnect,
  };

  enum event_e {
    Started,
    Connected,
    Resolved,
    ConnectListExhausted,
    ResolveListExhausted,
    ConnectFailed,
    ResolveFailed,
  };

  states_e state;

  struct endpoint_t {
    std::string hostname;
    std::string port;
  };
  std::list<endpoint_t> address_port_pending;

  // Currently conneting / connected endpoint
  endpoint_t resolve_next_dns_endpoint;
  // Needed at resolve_next_dns
  addrinfo *resolve_next_dns_sockaddress_list = nullptr;
  // Currently connected sock address
  addrinfo *resolve_next_dns_sockaddress = nullptr;
  // Connected to control port
  int connect_control_base_port = 0;

  rtpclient_t(std::string name);
  ~rtpclient_t();
  void sendto(const io_bytes &pb, rtppeer_t::port_e port);

  // receives event and call proper state function
  void state_machine(event_e event);

  // State functions
  void resolve_next_dns();
  void error_cant_connect();
  void connect_next_ip_port();
  void connect_control();
  void connect_midi();
  void disconnect_control();
  void connected_();

  void reset();

  /// try to connect to the given addresses in order
  bool connect_to(const std::vector<endpoint_t> &address_port);
  bool connect_to_next();

  // Try connect to one specific address and port
  bool connect_to(const std::string &address, const std::string &port);

  void connected();
  void send_ck0_with_timeout();

  void data_ready(rtppeer_t::port_e port);
};
} // namespace rtpmidid

template <>
struct fmt::formatter<rtpmidid::rtpclient_t::endpoint_t>
    : formatter<fmt::string_view> {
  auto format(const rtpmidid::rtpclient_t::endpoint_t &data,
              format_context &ctx) const {
    return formatter<fmt::string_view>::format(
        fmt::format("[endpoint_t [{}]:{}]", data.hostname, data.port), ctx);
  }
};

template <>
struct fmt::formatter<std::vector<rtpmidid::rtpclient_t::endpoint_t>>
    : formatter<fmt::string_view> {
  auto format(const std::vector<rtpmidid::rtpclient_t::endpoint_t> &data,
              format_context &ctx) const {
    std::string result;
    for (auto &endpoint : data) {
      result +=
          fmt::format("[endpoint_t [{}]:{}]", endpoint.hostname, endpoint.port);
    }
    return formatter<fmt::string_view>::format(result, ctx);
  }
};

template <>
struct fmt::formatter<std::list<rtpmidid::rtpclient_t::endpoint_t>>
    : formatter<fmt::string_view> {
  auto format(const std::list<rtpmidid::rtpclient_t::endpoint_t> &data,
              format_context &ctx) const {
    std::string result = "[";
    for (auto &endpoint : data) {
      result += fmt::format("[endpoint_t [{}]:{}] ", endpoint.hostname,
                            endpoint.port);
    }
    result += "]";
    return formatter<fmt::string_view>::format(result, ctx);
  }
};
