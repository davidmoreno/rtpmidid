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

namespace rtpmidid {
  /**
   * @short A RTP Client
   *
   * It connects to a remote address and port, do all the connection parts,
   * and emits midi events or on_disconnect if not valid.
   */
  class rtpclient{
  public:
    rtppeer peer;
    int control_socket;
    int midi_socket;
    uint16_t local_base_port;
    uint16_t remote_base_port;
    struct sockaddr_in peer_addr; // Will reuse addr, just changing the port
    poller_t::timer_t timer_ck;

    rtpclient(std::string name, const std::string &address, int16_t port);
    ~rtpclient();
    void reset();
    void sendto(const parse_buffer_t &pb, rtppeer::port_e port);

    void connect_to(std::string address, uint16_t port);
    void start_ck_1min_sync();

    void data_ready(rtppeer::port_e port);


    void on_midi(std::function<void(parse_buffer_t &)> f){
      peer.on_midi(f);
    }
    void on_connect(std::function<void(const std::string &)> f){
      peer.on_connect(f);
    }
    void on_send(std::function<void(const parse_buffer_t &, rtppeer::port_e)> f){
      peer.on_send(f);
    }
    void on_disconnect(std::function<void()> f){
      peer.on_disconnect(f);
    }

    void send_midi(parse_buffer_t &buffer){
      peer.send_midi(buffer);
    }
  };
}
