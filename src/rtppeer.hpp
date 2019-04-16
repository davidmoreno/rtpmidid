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
#include <functional>
#include <arpa/inet.h>

// A random int32. Should be configurable, so diferent systems, have diferent SSRC.
inline const uint32_t SSRC = 0x111f6c31;

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
    std::string local_name;
    std::string remote_name;
    struct sockaddr_in peer_addr; // Will reuse addr, just changing the port
    uint16_t seq_nr_ack;
    uint16_t seq_nr;
    uint16_t remote_seq_nr;
    uint64_t timestamp_start; // Time in ms
    uint64_t latency;
    std::function<void(parse_buffer_t &)> event_midi;
    std::function<void(void)> event_close;
    std::function<void(const std::string &)> event_connect;

    rtppeer(std::string _name, int startport);
    virtual ~rtppeer();

    void on_midi(std::function<void(parse_buffer_t &)> f){
      event_midi = f;
    }
    void on_close(std::function<void(void)> f){
      event_close = f;
    }
    void on_connect(std::function<void(const std::string &)> f){
      event_connect = f;
    }

    void control_data_ready();
    void midi_data_ready();
    void parse_command(parse_buffer_t &, int socket);
    void parse_feedback(parse_buffer_t &);
    void parse_command_ok(parse_buffer_t &, int socket);
    void parse_command_in(parse_buffer_t &, int socket);
    void parse_command_ck(parse_buffer_t &, int socket);
    void parse_command_by(parse_buffer_t &, int socket);
    void parse_midi(parse_buffer_t &);

    void send_midi(parse_buffer_t *buffer);
    void send_goodbye(int from_fd, int to_port);
    uint64_t get_timestamp();

    void sendto(int socket, const parse_buffer_t &b);
  };
}
