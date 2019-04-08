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

#include "./rtpserver.hpp"
#include "./poller.hpp"
#include "./exceptions.hpp"
#include "./logger.hpp"

using namespace rtpmidid;

rtpserver::rtpserver(std::string name, int16_t port) : rtppeer(std::move(name), port){
  DEBUG("RTP MIDI ports at 0.0.0.0:{} / 0.0.0.0:{}, with name: {} ({}, {})",
    port, port + 1, name, control_socket, midi_socket);
}

rtpserver::~rtpserver(){
}
