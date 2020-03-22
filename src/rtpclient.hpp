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

#pragma once

#include <string>
#include "./rtppeer.hpp"
#include "./poller.hpp"
#include "./signal.hpp"

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
  class rtpclient{
  public:
    rtppeer peer;
    // signal_t<> connect_failed_event;
    poller_t::timer_t connect_timer;
    int connect_count = 3;  // how many times we tried to connect, after 3, final fail.

    int control_socket;
    int midi_socket;
    struct sockaddr control_addr;
    struct sockaddr midi_addr;

    uint16_t local_base_port;
    uint16_t remote_base_port;
    poller_t::timer_t timer_ck;

    rtpclient(std::string name);
    ~rtpclient();
    void reset();
    void sendto(const parse_buffer_t &pb, rtppeer::port_e port);

    void connect_to(const std::string &address, const std::string &port);
    void start_ck_1min_sync();

    void data_ready(rtppeer::port_e port);
  };
}
