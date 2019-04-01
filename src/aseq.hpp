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
#include <vector>
#include <alsa/asoundlib.h>

namespace rtpmidid {
  class aseq {
  public:
    std::string name;
    snd_seq_t *seq;
    std::vector<int> fds; // Normally 1?

    aseq(std::string name);
    ~aseq();

    void read_ready();
    std::string get_client_name(snd_seq_addr_t *addr);

    uint8_t create_port(const std::string &name);
  };

  std::vector<std::string> get_ports(aseq *);
}
