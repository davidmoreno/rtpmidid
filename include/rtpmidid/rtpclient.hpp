/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
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
#include <string>

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
class rtpclient {

public:
  rtppeer peer;
  // signal_t<> connect_failed_event;
  poller_t::timer_t connect_timer;
  poller_t::timer_t ck_timeout;
  int connect_count =
      3; // how many times we tried to connect, after 3, final fail.

  int control_socket;
  int midi_socket;
  struct sockaddr control_addr;
  struct sockaddr midi_addr;

  uint16_t local_base_port;
  uint16_t remote_base_port;
  poller_t::timer_t timer_ck;
  /// A simple state machine. We need to send 6 CK one after another, and then
  /// every 10 secs.
  uint8_t timerstate;

  rtpclient(std::string name);
  ~rtpclient();
  void reset();
  void sendto(const io_bytes &pb, rtppeer::port_e port);

  void connect_to(const std::string &address, const std::string &port);
  void connected();
  void send_ck0_with_timeout();

  void data_ready(rtppeer::port_e port);
};
} // namespace rtpmidid
