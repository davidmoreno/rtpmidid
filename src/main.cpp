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

class main_t {
protected:
  std::shared_ptr<rtpmididns::midirouter_t> router;
  std::shared_ptr<rtpmididns::aseq_t> aseq;
  std::optional<rtpmididns::HwAutoAnnounce> hwautoannounce;
  rtpmididns::control_socket_t control;
  std::optional<rtpmididns::rtpmidi_remote_handler_t> rtpmidi_remote_handler;

public:
  // I want setup inside a try catch (and survive it), so I need a setup method
  void setup() {
    if (aseq.get() == nullptr)
      aseq =
          std::make_shared<rtpmididns::aseq_t>(rtpmididns::settings.alsa_name);
    if (rtpmididns::mdns.get() == nullptr)
      rtpmididns::mdns = std::make_unique<rtpmidid::mdns_rtpmidi_t>();
    router = std::make_shared<rtpmididns::midirouter_t>();
    control.router = router;
    control.aseq = aseq;
    control.mdns = rtpmididns::mdns;
    rtpmidi_remote_handler.emplace(router, aseq);

    setup_local_alsa_multilistener();
    setup_network_rtpmidi_multilistener();
    setup_network_rtpmidi_listener();
    setup_rawmidi_peers();

    hwautoannounce.emplace(aseq, router);
  }

  void close() { rtpmididns::mdns = nullptr; }

protected:
  void setup_local_alsa_multilistener() {
    // Create all the alsa network midipeers
    for (const auto &announce : rtpmididns::settings.alsa_announces) {
      router->add_peer(
          rtpmididns::make_local_alsa_multi_listener(announce.name, aseq));
    }
  }

  void setup_network_rtpmidi_multilistener() {
    // Create all the rtpmidi network midipeers
    for (const auto &announce : rtpmididns::settings.rtpmidi_announces) {
      router->add_peer(rtpmididns::make_network_rtpmidi_multi_listener(
          announce.name, announce.port, aseq));
    }
  }

  void setup_network_rtpmidi_listener() {
    // Connect to all static endpoints
    for (const auto &connect_to : rtpmididns::settings.connect_to) {
      router->add_peer(rtpmididns::make_local_alsa_listener(
          router, connect_to.name, connect_to.hostname, connect_to.port, aseq,
          connect_to.local_udp_port));
    }
  }

  void setup_rawmidi_peers() {
    for (const auto &rawmidi : rtpmididns::settings.rawmidi) {
      auto rawmidi_peer =
          rtpmididns::make_rawmidi_peer(rawmidi.name, rawmidi.device);
      router->add_peer(rawmidi_peer);
      std::shared_ptr<rtpmididns::midipeer_t> rtppeer;

      if (rawmidi.hostname.empty()) {
        INFO("Creating rawmidi peer={} as listener at udp_port={}",
             rawmidi.name, rawmidi.local_udp_port);
        rtppeer = rtpmididns::make_network_rtpmidi_listener(
            rawmidi.name, rawmidi.local_udp_port);
      } else {
        INFO("Creating rawmidi peer={} as client to hostname={} udp_port={}",
             rawmidi.name, rawmidi.hostname, rawmidi.remote_udp_port);
        rtppeer = rtpmididns::make_network_rtpmidi_client(
            rawmidi.name, rawmidi.hostname, rawmidi.remote_udp_port);
      }
      router->add_peer(rtppeer);

      router->connect(rawmidi_peer->peer_id, rtppeer->peer_id);
      router->connect(rtppeer->peer_id, rawmidi_peer->peer_id);
    }
  }
};

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

  main_t maindata;

  // SETUP
  try {
    maindata.setup();
  } catch (const std::exception &exc) {
    ERROR("Error on setup: {}", exc.what());
    return 1;
  } catch (...) {
    ERROR("Unhandled exception in setup!");
    return 1;
  }

  // MAIN RUN
  try {
    INFO("Waiting for connections.");
    while (rtpmidid::poller.is_open()) {
      rtpmidid::poller.wait();
    }
  } catch (const std::exception &exc) {
    ERROR("Unhandled exception: {}!", exc.what());
  } catch (...) {
    ERROR("Unhandled exception!");
  }

  maindata.close();

  INFO("FIN");
  return 0;
}
