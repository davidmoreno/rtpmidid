/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "rtpmidinetwork.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"

namespace rtpmididns {

extern std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;

rtpmidinetwork_t::rtpmidinetwork_t(const std::string &name,
                                   const std::string &port,
                                   rtpmididns::midirouter_t *router)
    : server(name, port) {
  if (mdns)
    mdns->announce_rtpmidi(name, server.control_port);
}
} // namespace rtpmididns
