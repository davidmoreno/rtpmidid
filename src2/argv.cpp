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

#include "settings.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <functional>
#include <iterator>
#include <rtpmidid/exceptions.hpp>
#include <string>
#include <unistd.h>

namespace rtpmididns {

#ifndef RTPMIDID_VERSION
#define RTPMIDID_VERSION "unknown"
#endif

const char *VERSION = RTPMIDID_VERSION;

const char *CMDLINE_HELP =
    ""
    "rtpmidid v{}/2\n"
    "Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.\n"
    "\n"
    "rtpmidi allows to use rtpmidi protocol to communicate with MIDI "
    "equipement \n"
    "using network equipiment. Recomended use is via ethernet cabling as with "
    "WiFi\n"
    "there is a lot more latency. Internet use has not been tested, but may "
    "also\n"
    "deliver high latency.\n"
    "\n"
    "Options:\n"
    "  --version                                  Show version\n"
    "  --help                                     Show this help\n"
    "  --name <name>                              Forces the alsa and rtpmidi "
    "name\n"
    "  --alsa-name <name>                         Forces the alsa name\n"
    "  --alsa-network-announcement=<true|false>   Enables / Disables the ALSA "
    "Network "
    "port to announce on the network. If you have a firewall you want this "
    "disabled and allow only connections from the network to rtpmidid, not "
    "announce new endpoints. Default: True\n"
    "  --rtpmidi-name <name>                      Forces the rtpmidi name\n"
    // "  --host <address>    My default IP. Needed to answer mDNS. Normally "
    // "guessed but may be attached to another ip.\n"
    "  --port <port>                              Opens local port as server. "
    "Default "
    "5004.\n"
    // "  --connect <address> Connects the given address. This is default, no "
    // "need for --connect\n"
    "  --control <path>                           Creates a control socket. "
    "Check "
    "CONTROL.md. Default `/var/run/rtpmidid/control.sock`\n"
    // "  address for connect:\n"
    // "  hostname            Connects to hostname:5004 port using rtpmidi\n"
    // "  hostname:port       Connects to a hostname on a given port\n"
    // "  name:hostname:port  Connects to a hostname on a given port and forces
    // a " "name for alsaseq\n"
    "\n";

bool str_to_bool(const std::string &value) {
  // conver t value to lowercase, so True becomes true
  std::string value_lowercase = value;
  for (auto &c : value_lowercase) {
    c = std::tolower(c);
  }
  if (value_lowercase == "true") {
    return true;
  }
  if (value_lowercase == "false") {
    return false;
  }
  throw rtpmidid::exception("Invalid boolean value: {}", value);
}

void parse_argv(int argc, char **argv) {
  int cargc = 1;
  auto arg = std::string(argv[0]);
  bool parsed_something = false;
  auto parse_arg2 = [&argv, &cargc, &argc, &parsed_something](
                        const std::string &name,
                        const std::function<void(const std::string &)> &fn) {
    if (cargc >= argc) {
      return;
    }

    if (name == argv[cargc]) {
      parsed_something = true;
      fn(argv[cargc + 1]);
      cargc += 2;
    }
    if (cargc >= argc) {
      return;
    }
    auto key = std::string(argv[cargc]);
    auto keyeq = fmt::format("{}=", name);
    if (key.substr(0, keyeq.length()) == keyeq) {
      parsed_something = true;
      fn(key.substr(keyeq.length()));
      cargc += 2;
    }
  };

  auto parse_arg1 = [&argv, &cargc, &argc,
                     &parsed_something](const std::string &name,
                                        const std::function<void()> &fn) {
    if (cargc >= argc) {
      return;
    }
    if (name == argv[cargc]) {
      parsed_something = true;
      fn();
      cargc++;
    }
  };

  while (cargc < argc) {
    parsed_something = false;

    parse_arg2("--port", [](const std::string &value) {
      settings.rtpmidid_port = std::string(value);
    });
    parse_arg2("--name", [](const std::string &value) {
      settings.rtpmidid_name = std::string(value);
      settings.alsa_name = std::string(value);
    });
    parse_arg2("--alsa-name", [](const std::string &value) {
      settings.rtpmidid_name = std::string(value);
      settings.alsa_name = std::string(value);
    });
    parse_arg2("--alsa-network-announcement", [](const std::string &value) {
      settings.alsa_network = str_to_bool(value);
    });
    parse_arg2("--rtpmidid-name", [](const std::string &value) {
      settings.rtpmidid_name = std::string(value);
      settings.alsa_name = std::string(value);
    });
    parse_arg2("--control", [](const std::string &value) {
      settings.control_filename = std::string(value);
    });
    parse_arg1("--version", []() {
      fmt::print("rtpmidid version {}/2\n", VERSION);
      exit(0);
    });
    parse_arg1("--help", []() {
      fmt::print(CMDLINE_HELP, VERSION);
      exit(0);
    });
    if (!parsed_something) {
      throw rtpmidid::exception("Unknown option: {}", argv[cargc]);
    }
  }

  if (settings.rtpmidid_name == "") {
    char hostname[256];
    gethostname(hostname, std::size(hostname));
    settings.rtpmidid_name = hostname;
  }
}

} // namespace rtpmididns
