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

#include <iostream>
#include <random>
#include <signal.h>
#include <unistd.h>

#include "./logger.hpp"
#include "./rtpmidid.hpp"
#include "./poller.hpp"
#include "./stringpp.hpp"

const auto VERSION = "alpha 19.04";
const auto CMDLINE_HELP = ""
"Real Time Protocol Music Instrument Digital Interface Daemon - alpha 19.04\n"
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
"  hostname            Connects to hostname:5400 port using rtpmidi\n"
"  hostname:port       Connects to a hostname on a given port\n"
"  name:hostname:port  Connects to a hostname on a given port and forces a name for alsaseq\n"
"\n"
;


using namespace std;

struct options_t {
  std::string name;
  std::vector<std::string> connect_to;
};
options_t parse_options(int argc, char **argv);


void sigterm_f(int){
  INFO("SIGTERM received. Closing.");
  rtpmidid::poller.close();
}
void sigint_f(int){
  INFO("SIGINT received. Closing.");
  rtpmidid::poller.close();
}

int main(int argc, char **argv){

    // We dont need crypto rand, just some rand
    srand(time(NULL));

    signal(SIGINT, sigint_f);
    signal(SIGTERM, sigterm_f);

    auto options = parse_options(argc-1, argv+1);

    INFO("Real Time Protocol Music Instrument Digital Interface Daemon - {}", VERSION);
    INFO("(C) 2019 David Moreno Montero <dmoreno@coralbits.com>");

    try{
      auto rtpmidid = ::rtpmidid::rtpmidid(options.name);

      for (auto &connect_to: options.connect_to){
        auto s = rtpmidid::split(connect_to, ':');
        if (s.size() == 1){
          rtpmidid.add_rtpmidi_client(s[0], s[0], 5004);
        }
        else if (s.size() == 2){
          rtpmidid.add_rtpmidi_client(s[0], s[1], 5004);
        }
        else if (s.size() == 3){
          rtpmidid.add_rtpmidi_client(s[0], s[1], stoi(s[2].c_str()));
        }
        else {
          ERROR("Invalid remote address. Format is ip, name:ip, or name:ip:port. {}", s.size());
          return 1;
        }
      }

      while(rtpmidid::poller.is_open()){
        rtpmidid::poller.wait();
      }
    } catch (const std::exception &e){
      ERROR("{}", e.what());
      return 1;
    }
    DEBUG("FIN");
    return 0;
}


options_t parse_options(int argc, char **argv){
  options_t opts;

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
      }
    } else {
      switch(prevopt){
        case '\0':
          opts.connect_to.push_back(argv[i]);
          break;
        case 'n':
          opts.name = argv[i];
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
    gethostname(hostname, size(hostname));
    opts.name = hostname;
  }

  return opts;
}
