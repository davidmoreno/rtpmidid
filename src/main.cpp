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
#include "midipeer.hpp"
#include "rtpmidid/exceptions.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidiremotehandler.hpp"
#include "settings.hpp"
#include <chrono>
#include <signal.h>
#include <unistd.h>

namespace rtpmididns {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
settings_t settings;
void parse_argv(const std::vector<std::string> &argv);
} // namespace rtpmididns

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
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

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  rtpmididns::parse_argv(std::move(args));
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
    control.mdns = rtpmididns::mdns;
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
          router, connect_to.name, connect_to.hostname, connect_to.port, aseq,
          connect_to.local_udp_port));
    }
    for (const auto &devmidi : rtpmididns::settings.devmidi) {
      auto devmidi_peer =
          rtpmididns::make_rawmidi_peer(devmidi.name, devmidi.device);
      router->add_peer(devmidi_peer);
      auto rtpclient_peer = rtpmididns::make_network_rtpmidi_client(
          devmidi.connect_to.name, devmidi.connect_to.hostname,
          devmidi.connect_to.port);
      router->add_peer(rtpclient_peer);

      if (!devmidi.connect_to.hostname.empty()) {
        router->connect(devmidi_peer->peer_id, rtpclient_peer->peer_id);
        router->connect(rtpclient_peer->peer_id, devmidi_peer->peer_id);
      }
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
  rtpmididns::mdns = nullptr;

  INFO("FIN");
  return 0;
}
