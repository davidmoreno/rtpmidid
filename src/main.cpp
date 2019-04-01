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

#include <iostream>
#include <random>
#include <signal.h>

#include "./logger.hpp"
#include "./rtpmidid.hpp"
#include "./poller.hpp"
#include "./stringpp.hpp"

using namespace std;

const auto MYNAME = "rtpmidid";

void sigterm_f(int){
  INFO("SIGTERM received. Closing.");
  rtpmidid::poller.close();
}
void sigint_f(int){
  INFO("SIGINT received. Closing.");
  rtpmidid::poller.close();
}

int main(int argc, char **argv){
    INFO("Real Time Protocol Music Industry Digital Interface Daemon - v0.1");
    INFO("(C) 2019 David Moreno Montero <dmoreno@coralbits.com>");

    // We dont need crypto rand, just some rand
    srand(time(NULL));

    signal(SIGINT, sigint_f);
    signal(SIGTERM, sigterm_f);
    try{
      auto rtpmidid = ::rtpmidid::rtpmidid(MYNAME);

      for (int n=1; n<argc; n++){
        auto s = rtpmidid::split(argv[n], ':');
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
