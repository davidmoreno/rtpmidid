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
#include "./udppeer.hpp"
#include <list>
#include <string>

extern "C" {
struct addrinfo;
}

namespace rtpmidid {
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
  struct endpoint_t {
    std::string hostname;
    std::string port;
  };

#include "rtpclient_statemachine.hpp"

public:
  poller_t::timer_t timer;

  uint16_t local_base_port = 0;
  std::string local_base_port_str =
      "0"; // This can be changed before connec tto force a port

  uint8_t ck_count = 0;
  rtppeer_t::send_event_t::connection_t send_connection;
  rtppeer_t::ck_event_t::connection_t ck_connection;

  rtppeer_t::status_change_event_t::connection_t
      control_connected_event_connection;
  rtppeer_t::status_change_event_t::connection_t
      midi_connected_event_connection;

  signal_t<const std::string &, rtppeer_t::status_e> connected_event;

  udppeer_t control_peer;
  udppeer_t midi_peer;
  network_address_t control_address;
  network_address_t midi_address;
  udppeer_t::on_read_t::connection_t control_on_read_connection;
  udppeer_t::on_read_t::connection_t midi_on_read_connection;

  std::list<endpoint_t> address_port_known;   // All known address and ports
  std::list<endpoint_t> address_port_pending; // Pending to try to connect to

  // Currently conneting / connected endpoint
  endpoint_t resolve_next_dns_endpoint;
  // Needed at resolve_next_dns
  network_address_list_t resolve_next_dns_sockaddress_list;
  // Iterator to the current endpoint
  network_address_list_t::iterator_t resolve_next_dns_sockaddress_list_I;

public:
  rtpclient_t(std::string name);
  ~rtpclient_t();

  void data_ready(rtppeer_t::port_e port);
  void sendto(const io_bytes &pb, rtppeer_t::port_e port);
  void reset();

  // Public interface
  void add_server_address(const std::string &address, const std::string &port);
  void add_server_addresses(
      const std::vector<rtpmidid::rtpclient_t::endpoint_t> &endpoints);
  void connect();
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

// state machine formatter
template <>
struct fmt::formatter<rtpmidid::rtpclient_t::state_e>
    : formatter<fmt::string_view> {
  auto format(const rtpmidid::rtpclient_t::state_e &data, format_context &ctx) {
    const char *ret = rtpmidid::rtpclient_t::to_string(data);
    return formatter<fmt::string_view>::format(ret, ctx);
  }
};

// event formatter
template <>
struct fmt::formatter<rtpmidid::rtpclient_t::event_e>
    : formatter<fmt::string_view> {
  auto format(const rtpmidid::rtpclient_t::event_e &data, format_context &ctx) {
    const char *ret = rtpmidid::rtpclient_t::to_string(data);
    return formatter<fmt::string_view>::format(ret, ctx);
  }
};
