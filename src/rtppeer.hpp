/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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
#include <arpa/inet.h>

namespace rtpmidid {
  struct parse_buffer_t;

  class rtppeer {
  public:
    // Commands, th id is the same chars as the name
    enum commands_e {
      IN = 0x494e,
      OK = 0x4f4b,
      NO = 0x4e4f,
      BY = 0x4259,
      CK = 0x434b,
      RS = 0x5253
    };

    int control_socket;
    int midi_socket;
    uint16_t local_base_port;
    uint16_t remote_base_port;
    uint32_t initiator_id;
    uint32_t remote_ssrc;
    std::string name;
    struct sockaddr_in peer_addr; // Will reuse addr, just changing the port

    rtppeer(std::string _name, int startport);
    virtual ~rtppeer();

    virtual void control_data_ready();
    virtual void midi_data_ready();
    void parse_command(parse_buffer_t &, int port);
    void parse_command_ok(parse_buffer_t &, int port);
  };
}
