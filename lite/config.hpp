/**
 * rtpmidid-lite: one ALSA sequencer port exported over RTP-MIDI.
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include <chrono>
#include <iosfwd>
#include <rtpmidid/logger.hpp>
#include <string>
#include <vector>

namespace rtpmidid_lite {

/// A remote RTP-MIDI endpoint, as given to --connect.
struct endpoint_t {
  std::string host;
  std::string port;
};

struct config_t {
  /// ALSA client and port name, and the mDNS service name. Empty until
  /// parse_args resolves it to the hostname.
  std::string name;
  /// UDP control port to listen on. The MIDI port is this plus one.
  std::string port = "5004";
  std::vector<endpoint_t> connect_to;
  bool list_peers = false;
  /// Announce and browse with mDNS. Without it --list finds nothing.
  bool mdns = true;
  std::chrono::milliseconds list_timeout{std::chrono::seconds(3)};
  rtpmidid::logger_level_t log_level = rtpmidid::logger_level_t::INFO;
};

enum class parse_status_e {
  RUN,       ///< Run the daemon.
  LIST,      ///< --list: discover mDNS peers, print them and exit.
  EXIT_OK,   ///< --help or --version already printed.
  EXIT_ERROR ///< Invalid arguments, an error was already printed.
};

/// Parse the command line (without the program name) into `out`.
parse_status_e parse_args(const std::vector<std::string> &args, config_t *out);

/// The system hostname, or "rtpmidid-lite" when it can not be determined. This
/// is the default name when --name is not given.
std::string hostname();

/// Parse "host", "host:port" or "[ipv6]:port". A missing or empty port becomes
/// `default_port`.
endpoint_t parse_endpoint(const std::string &value,
                          const std::string &default_port = "5004");

void print_usage(std::ostream &out);
void print_version(std::ostream &out);

} // namespace rtpmidid_lite
