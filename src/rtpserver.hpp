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

#include "./rtppeer.hpp"

namespace rtpmidid{
  class rtpserver{
  public:
    rtppeer peer;

    uint16_t local_base_port;

    rtpserver(std::string name, int16_t port);
    ~rtpserver();

    void on_midi(std::function<void(parse_buffer_t &)> f){
    }
    void on_close(std::function<void(void)> f){
    }
    void on_connect(std::function<void(const std::string &)> f){
    }
    void on_send(std::function<void(rtppeer::port_e, const parse_buffer_t &)> f){
    }
    void send_midi(parse_buffer_t *buffer){
    }

  };
}
