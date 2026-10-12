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

#include "config.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <unistd.h>

namespace rtpmidid_lite {

namespace {
#ifndef RTPMIDID_VERSION
// NOLINTNEXTLINE
#define RTPMIDID_VERSION "unknown"
#endif

constexpr const char *DEFAULT_PORT = "5004";

bool valid_port(const std::string &value) {
  if (value.empty() || value.size() > 5)
    return false;
  for (char c : value) {
    if (c < '0' || c > '9')
      return false;
  }
  int port = 0;
  try {
    port = std::stoi(value);
  } catch (const std::exception &) {
    return false;
  }
  return port > 0 && port <= 65535;
}
} // namespace

std::string hostname() {
  constexpr size_t MAX_HOSTNAME_SIZE = 256;
  std::array<char, MAX_HOSTNAME_SIZE> buffer{};
  if (::gethostname(buffer.data(), buffer.size() - 1) != 0)
    return "rtpmidid-lite";
  std::string name(buffer.data());
  if (name.empty())
    return "rtpmidid-lite";
  return name;
}

endpoint_t parse_endpoint(const std::string &value,
                          const std::string &default_port) {
  endpoint_t ret{value, default_port};
  if (value.empty())
    return ret;

  // [ipv6]:port or [ipv6]
  if (value.front() == '[') {
    auto close = value.find(']');
    if (close == std::string::npos)
      return ret;
    ret.host = value.substr(1, close - 1);
    if (close + 2 <= value.size() && value[close + 1] == ':') {
      ret.port = value.substr(close + 2);
    }
    if (ret.port.empty())
      ret.port = default_port;
    return ret;
  }

  auto first_colon = value.find(':');
  if (first_colon == std::string::npos) {
    return ret;
  }
  // A second colon means a bare IPv6 address, not a host:port pair.
  if (value.find(':', first_colon + 1) != std::string::npos) {
    ret.host = value;
    ret.port = default_port;
    return ret;
  }

  ret.host = value.substr(0, first_colon);
  ret.port = value.substr(first_colon + 1);
  if (ret.port.empty())
    ret.port = default_port;
  return ret;
}

void print_usage(std::ostream &out) {
  out << "Usage: rtpmidid-lite [OPTIONS]\n"
      << "\n"
      << "Exports a single ALSA sequencer port over Apple-style RTP-MIDI.\n"
      << "Everything arriving at the ALSA port is sent to every RTP-MIDI\n"
      << "connection, and everything arriving from the network is sent to\n"
      << "the ALSA port.\n"
      << "\n"
      << "Options:\n"
      << "  --name NAME             ALSA and mDNS name (default: the "
         "hostname)\n"
      << "  --port PORT             UDP port to listen on (default 5004)\n"
      << "  --connect HOST[:PORT]   Connect to a remote peer. Repeatable.\n"
      << "  --list                  List mDNS-discovered RTP-MIDI peers and "
         "exit\n"
      << "  --list-timeout SECONDS  Discovery time for --list (default 3)\n"
      << "  --no-mdns               Do not announce or browse with mDNS\n"
      << "  --log-level LEVEL       debug, info, warning or error\n"
      << "  -h, --help              Show this help and exit\n"
      << "  -v, --version           Show the version and exit\n";
}

void print_version(std::ostream &out) {
  out << "rtpmidid-lite " << RTPMIDID_VERSION << "\n";
}

parse_status_e parse_args(const std::vector<std::string> &args, config_t *out) {
  bool list = false;
  bool name_given = false;

  auto error = [](const std::string &message) {
    std::cerr << "rtpmidid-lite: " << message << "\n";
    std::cerr << "Try 'rtpmidid-lite --help' for more information.\n";
    return parse_status_e::EXIT_ERROR;
  };

  for (size_t i = 0; i < args.size(); i++) {
    const auto &arg = args[i];
    auto value = [&args, &i, &error, &arg]() -> std::optional<std::string> {
      if (i + 1 >= args.size()) {
        error("missing value for " + arg);
        return {};
      }
      return args[++i];
    };

    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return parse_status_e::EXIT_OK;
    }
    if (arg == "-v" || arg == "--version") {
      print_version(std::cout);
      return parse_status_e::EXIT_OK;
    }
    if (arg == "--list") {
      list = true;
      continue;
    }
    if (arg == "--no-mdns") {
      out->mdns = false;
      continue;
    }
    if (arg == "--name") {
      auto v = value();
      if (!v)
        return parse_status_e::EXIT_ERROR;
      out->name = *v;
      name_given = true;
      continue;
    }
    if (arg == "--port") {
      auto v = value();
      if (!v)
        return parse_status_e::EXIT_ERROR;
      if (!valid_port(*v))
        return error("invalid port: " + *v);
      out->port = *v;
      continue;
    }
    if (arg == "--connect") {
      auto v = value();
      if (!v)
        return parse_status_e::EXIT_ERROR;
      auto endpoint = parse_endpoint(*v, DEFAULT_PORT);
      if (endpoint.host.empty())
        return error("invalid --connect value: " + *v);
      if (!valid_port(endpoint.port))
        return error("invalid port in --connect value: " + *v);
      out->connect_to.push_back(std::move(endpoint));
      continue;
    }
    if (arg == "--list-timeout") {
      auto v = value();
      if (!v)
        return parse_status_e::EXIT_ERROR;
      try {
        auto seconds = std::stod(*v);
        if (seconds < 0)
          return error("--list-timeout must not be negative: " + *v);
        out->list_timeout =
            std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
      } catch (const std::exception &) {
        return error("invalid --list-timeout: " + *v);
      }
      continue;
    }
    if (arg == "--log-level") {
      auto v = value();
      if (!v)
        return parse_status_e::EXIT_ERROR;
      try {
        out->log_level = rtpmidid::str_to_log_level(*v);
      } catch (const std::exception &e) {
        return error(e.what());
      }
      continue;
    }

    return error("unknown argument: " + arg);
  }

  if (!name_given)
    out->name = hostname();

  out->list_peers = list;
  return list ? parse_status_e::LIST : parse_status_e::RUN;
}

} // namespace rtpmidid_lite
