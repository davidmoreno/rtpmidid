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

#include "aseq.hpp"
#include "control_socket.hpp"
#include "factory.hpp"
#include "hwautoannounce.hpp"
#include "rtpmidid/exceptions.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidiremotehandler.hpp"
#include "settings.hpp"
#include <chrono>
#include <signal.h>
#include <unistd.h>

namespace rtpmididns {
std::unique_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
settings_t settings;
void parse_argv(int argc, char **argv);
} // namespace rtpmididns

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
  rtpmididns::parse_argv(argc, argv);
  std::optional<rtpmididns::HwAutoAnnounce> hwautoannounce;

  signal(SIGINT, sigint_f);
  signal(SIGTERM, sigterm_f);

  try {
    auto aseq =
        std::make_shared<rtpmididns::aseq_t>(rtpmididns::settings.alsa_name);
    rtpmididns::mdns = std::make_unique<rtpmidid::mdns_rtpmidi_t>();
    auto router = std::make_shared<rtpmididns::midirouter_t>();
    rtpmididns::control_socket_t control;
    control.router = router;
    control.aseq = aseq;
    rtpmididns::rtpmidi_remote_handler_t rtpmidi_remote_handler(router, aseq);

    // Create all the alsa network midipeers
    for (const auto &announce : rtpmididns::settings.alsa_announces) {
      router->add_peer(
          rtpmididns::make_local_alsa_multi_listener(announce.name, aseq));
    }
    // Create all the rtpmidi network midipeers
    for (const auto &announce : rtpmididns::settings.rtpmidi_announces) {
      router->add_peer(rtpmididns::make_network_rtpmidi_multi_listener(
          announce.name, announce.port, aseq));
    }
    // Connect to all static endpoints
    for (const auto &connect_to : rtpmididns::settings.connect_to) {
      router->add_peer(rtpmididns::make_local_alsa_listener(
          router, connect_to.name, connect_to.hostname, connect_to.port, aseq));
    }

    hwautoannounce.emplace(aseq, router);

    INFO("Waiting for connections.");
    while (rtpmidid::poller.is_open()) {
      rtpmidid::poller.wait();
    }
  } catch (const std::exception &exc) {
    ERROR("Unhandled exception: {}!", exc.what());
  } catch (...) {
    ERROR("Unhandled exception!");
  }
  rtpmididns::mdns.reset(nullptr);

  INFO("FIN");
  return 0;
}
