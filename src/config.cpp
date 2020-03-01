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
#include <unistd.h>

#include "./config.hpp"
#include "./logger.hpp"
#include "./stringpp.hpp"

using namespace rtpmidid;

const char *::rtpmidid::VERSION = "alpha 19.05";
const char *CMDLINE_HELP = ""
"Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.\n"
"\n"
"rtpmidi allows to use rtpmidi protocol to communicate with MIDI equipement \n"
"using network equipiment. Recomended use is via ethernet cabling as with WiFi\n"
"there is a lot more latency. Internet use has not been tested, but may also\n"
"deliver high latency.\n"
"\n"
"Options:\n"
"  --version           Show version\n"
"  --help              Show this help\n"
"  --name <name>       Forces a rtpmidi name\n"
"  --host <address>    My default IP. Needed to answer mDNS. Normally guessed but may be attached to another ip.\n"
"  --port <port>       Opens local port as server. Default 5400. Can set several.\n"
"  --connect <address> Connects the given address. This is default, no need for --connect\n"
"  --control <path>    Creates a control socket. Check CONTROL.md. Default `/var/run/rtpmidid/control.sock`\n"
"  address for connect:\n"
"  hostname            Connects to hostname:5400 port using rtpmidi\n"
"  hostname:port       Connects to a hostname on a given port\n"
"  name:hostname:port  Connects to a hostname on a given port and forces a name for alsaseq\n"
"\n"
;

typedef enum {
  ARG_NONE=0,
  ARG_NAME,
  ARG_HOST,
  ARG_PORT,
  ARG_CONNECT,
  ARG_CONTROL,
} optnames_e;

config_t rtpmidid::parse_cmd_args(int argc, char **argv){
  config_t opts;

  opts.host = "0.0.0.0";
  opts.control = "/var/run/rtpmidid/control.sock";

  optnames_e prevopt = ARG_NONE;
  for (auto i=0; i<argc; i++){
    auto len = strlen(argv[i]);
    if (len == 0){
      continue;
    }
    if (prevopt == ARG_NONE){
      std::string argname{argv[i]};
      if (argname == "--help"){
        fmt::print(CMDLINE_HELP);
        exit(0);
      }
      if (argname == "--version"){
        INFO("rtpmidid version {}", VERSION);
        exit(0);
      }
      if (argname == "--name") {
        prevopt = ARG_NAME;
      } else if (argname == "--host") {
        prevopt = ARG_HOST;
      } else if (argname == "--port") {
        prevopt = ARG_PORT;
      } else if (argname == "--connect") {
        prevopt = ARG_CONNECT;
      } else if (argname == "--control") {
        prevopt = ARG_CONTROL;
      } else if (startswith(argname, "--")) {
        ERROR("Unknown option. Check options with --help.");
      } else {
        DEBUG("Implicit connect to: {}", argname);
        opts.connect_to.push_back(argname);
      }
    } else {
      switch(prevopt){
        case ARG_NONE:
        case ARG_CONNECT:
          opts.connect_to.push_back(argv[i]);
          DEBUG("Explicit connect to: {}", argv[i]);
          break;
        case ARG_NAME:
          opts.name = argv[i];
          INFO("Set RTP MIDI name to {}", opts.name);
          break;
        case ARG_HOST:
          opts.host = argv[i];
          INFO("Set RTP MIDI listen host to {}", opts.host);
          break;
        case ARG_PORT:
          opts.ports.push_back(argv[i]);
          break;
        case ARG_CONTROL:
          opts.control = argv[i];
          break;
      }
      prevopt = ARG_NONE;
    }
  }

  if (opts.name.size() == 0){
    char hostname[256];
    gethostname(hostname, std::size(hostname));
    opts.name = hostname;
  }

  if (opts.ports.size() == 0){
    opts.ports.push_back("5004");
  }

  return opts;
}
