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
#include <exception>
#include <fmt/format.h>
#include <string>
#include <vector>

namespace rtpmidid {
extern const char *VERSION;

/**
 * @short All rtpmidi options in a nice struct to pass around
 *
 * This allows easy read config and parse command line and generate the
 * rtpmidid object.
 */
struct config_t {
  std::string name;
  std::vector<std::string> connect_to;
  // Create clients at this ports to start with. Later will see.
  std::vector<std::string> ports;
  std::string host;
  std::string control;
};
config_t parse_cmd_args(int argc, char **argv);
} // namespace rtpmidid
