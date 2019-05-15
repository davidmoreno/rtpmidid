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
  class rtpclient : public rtppeer {
  public:
    poller_t::timer_t timer_ck;

    rtpclient(std::string name, const std::string &address, int16_t port);
    virtual ~rtpclient();

    bool connect_to(int socketfd, int16_t port);
    void start_ck_1min_sync();
  };
}
