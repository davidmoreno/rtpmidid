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

using namespace rtpmidid;

const char *::rtpmidid::VERSION = "alpha 19.04";
const char *CMDLINE_HELP = ""
"Real Time Protocol Music Instrument Digital Interface Daemon - alpha 19.05\n"
"(C) 2019 David Moreno Montero <dmoreno@coralbits.com>\n\n"
"Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.\n"
"\n"
"rtpmidi allows to use rtpmidi protocol to communicate with MIDI equipement \n"
"using network equipiment. Recomended use is via ethernet cabling as with WiFi\n"
"there is a lot more latency. Internet use has not been tested, but may also\n"
"deliver high latency.\n"
"\n"
"Options:\n"
"  -v                  Show version\n"
"  -h                  Show this help\n"
"  -n name             Forces a rtpmidi name\n"
"  -p port             Opens local port as server. Default 5400. Can set several.\n"
"  -a                  Toggle automatic create local ports at connections (default True).\n"
//"  -m count            Max automatic rtpmidi ports to create. Default 26."
"  hostname            Connects to hostname:5400 port using rtpmidi\n"
"  hostname:port       Connects to a hostname on a given port\n"
"  name:hostname:port  Connects to a hostname on a given port and forces a name for alsaseq\n"
"\n"
;


config_t rtpmidid::parse_cmd_args(int argc, char **argv){
  config_t opts;

  char prevopt = '\0';
  for (auto i=0; i<argc; i++){
    auto len = strlen(argv[i]);
    if (len == 0){
      continue;
    }
    if (argv[i][0] == '-' && len == 2){
      prevopt = argv[i][1];
      switch(prevopt){
        case 'v':
          INFO("rtpmidid version {}", VERSION);
          exit(0);
          break;
        case 'h':
          fmt::print(CMDLINE_HELP);
          exit(0);
          break;
        case 'a':
          opts.automatic_create_ports = !opts.automatic_create_ports;
          break;
      }
    } else {
      switch(prevopt){
        case '\0':
          opts.connect_to.push_back(argv[i]);
          break;
        case 'n':
          opts.name = argv[i];
          break;
        case 'p':
          opts.ports.push_back(atoi(argv[i]));
          break;
        default:
          ERROR("Unknown option. Check options with -h.");
          exit(1);
          break;
      }
    }
  }

  if (opts.name.size() == 0){
    char hostname[256];
    gethostname(hostname, std::size(hostname));
    opts.name = hostname;
  }

  return opts;
}
