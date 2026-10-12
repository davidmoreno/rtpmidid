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
#include "daemon.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/poller.hpp>
#include <streambuf>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool exiting = false;

void handle_signal(int) {
  if (exiting)
    std::exit(1);
  exiting = true;
  INFO("Signal received, closing.");
  rtpmidid::poller.close();
}
} // namespace

namespace rtpmidid_lite {

namespace {
/// Send everything written to std::cout (the logger uses it too) to stderr, so
/// `--list` can keep stdout as a clean, parseable peer list.
struct logs_to_stderr_t {
  std::streambuf *previous;
  logs_to_stderr_t() : previous(std::cout.rdbuf(std::cerr.rdbuf())) {}
  ~logs_to_stderr_t() { std::cout.rdbuf(previous); }
};
} // namespace

/// Discover RTP-MIDI peers with mDNS for a while and print them.
int list_peers(const config_t &config) {
  std::vector<rtpmidid::remote_announcement_t> remotes;
  {
    logs_to_stderr_t logs_to_stderr;

    rtpmidid::mdns_rtpmidi_t mdns;
    const auto deadline =
        std::chrono::steady_clock::now() + config.list_timeout;
    while (rtpmidid::poller.is_open() &&
           std::chrono::steady_clock::now() < deadline) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      rtpmidid::poller.wait(left);
    }
    remotes = mdns.remote_announcements;
  }
  std::sort(remotes.begin(), remotes.end(), [](const auto &a, const auto &b) {
    return std::tie(a.name, a.address, a.port) <
           std::tie(b.name, b.address, b.port);
  });
  remotes.erase(std::unique(remotes.begin(), remotes.end(),
                            [](const auto &a, const auto &b) {
                              return a.name == b.name &&
                                     a.address == b.address && a.port == b.port;
                            }),
                remotes.end());

  if (remotes.empty()) {
    std::cout << "No RTP-MIDI peers discovered.\n";
    return 0;
  }

  std::cout << "RTP-MIDI peers:\n";
  for (const auto &remote : remotes)
    std::cout << "  " << remote.name << "  " << remote.address << ":"
              << remote.port << "\n";
  return 0;
}

} // namespace rtpmidid_lite

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++)
    args.emplace_back(argv[i]);

  rtpmidid_lite::config_t config;
  const auto status = rtpmidid_lite::parse_args(args, &config);
  if (status == rtpmidid_lite::parse_status_e::EXIT_OK)
    return 0;
  if (status == rtpmidid_lite::parse_status_e::EXIT_ERROR)
    return 2;

  rtpmidid::logger2.set_log_level(config.log_level);

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  if (status == rtpmidid_lite::parse_status_e::LIST)
    return rtpmidid_lite::list_peers(config);

  try {
    rtpmidid_lite::daemon_t daemon(std::move(config));
    daemon.start();
    INFO("Waiting for connections. Press Ctrl-C to stop.");
    while (rtpmidid::poller.is_open())
      rtpmidid::poller.wait();
  } catch (const std::exception &e) {
    ERROR("{}", e.what());
    return 1;
  }

  INFO("Bye.");
  return 0;
}
