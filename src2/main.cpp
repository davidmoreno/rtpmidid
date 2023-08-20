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

#include "alsanetwork.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "rtpmidid/poller.hpp"
#include <signal.h>
#include <unistd.h>

namespace rtpmididns {
std::unique_ptr<::rtpmidid::mdns_rtpmidi> mdns;
}

static bool exiting = false;

void sigterm_f(int) {
  if (exiting) {
    exit(1);
  }
  exiting = true;
  INFO("SIGTERM received. Closing.");
  rtpmidid::poller.close();
}
void sigint_f(int) {
  if (exiting) {
    exit(1);
  }
  exiting = true;
  INFO("SIGINT received. Closing.");
  rtpmidid::poller.close();
}

int main(int argc, char **argv) {
  signal(SIGINT, sigint_f);
  signal(SIGTERM, sigterm_f);

  INFO("Waiting for connections.");
  try {
    rtpmididns::mdns = std::make_unique<rtpmidid::mdns_rtpmidi>();
    rtpmididns::midirouter_t router;
    rtpmididns::alsanetwork_t alsanetwork("rtpmidid", &router);

    while (rtpmidid::poller.is_open()) {
      rtpmidid::poller.wait();
    }
  } catch (...) {
    ERROR("Unhandled exception!");
  }
  rtpmididns::mdns.reset(nullptr);

  INFO("FIN");
}
