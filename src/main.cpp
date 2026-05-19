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

#include "argv.hpp"
#include "aseq.hpp"
#include "control_socket.hpp"
#include "web_server.hpp"
#include "factory.hpp"
#include "hwautoannounce.hpp"
#include "local_rawmidi_peer.hpp"
#include "midipeer.hpp"
#include "rtpmidid/exceptions.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/dns_resolver.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/shutdown_signals.hpp"
#include "rtpmidiremotehandler.hpp"
#include "settings.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cxxabi.h>
#include <execinfo.h>
#include <functional>
#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <sstream>
#include <vector>

namespace rtpmididns {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::shared_ptr<::rtpmidid::mdns_rtpmidi_t> mdns;
} // namespace rtpmididns

void print_stacktrace() {
  void *array[50];
  size_t size = backtrace(array, 50);
  char **strings = backtrace_symbols(array, size);

  ERROR("=== STACKTRACE ({} frames) ===", size);
  for (size_t i = 0; i < size; i++) {
    std::string symbol(strings[i]);
    
    // Parse the backtrace line format: "executable(function+offset) [address]"
    // Example: "./rtpmidid(_Z10some_funcv+0x123) [0x456789]"
    // Or: "/path/to/lib.so(function+offset) [0x456789]"
    
    size_t func_start = symbol.find('(');
    size_t func_end = symbol.find('+', func_start);
    size_t addr_start = symbol.find('[');
    
    if (func_start != std::string::npos && func_end != std::string::npos) {
      std::string before_func = symbol.substr(0, func_start);
      std::string mangled = symbol.substr(func_start + 1, func_end - func_start - 1);
      std::string offset_part;
      std::string addr_part;
      
      if (addr_start != std::string::npos) {
        offset_part = symbol.substr(func_end, addr_start - func_end);
        addr_part = symbol.substr(addr_start);
      } else {
        offset_part = symbol.substr(func_end);
      }
      
      // Try to demangle C++ symbols
      if (!mangled.empty() && mangled[0] != '?') {
        int status = 0;
        char *demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled) {
          std::string result = before_func + "(" + demangled + offset_part;
          if (!addr_part.empty()) {
            result += addr_part;
          }
          ERROR("  [{}] {}", i, result);
          free(demangled);
        } else {
          // Couldn't demangle, print original
          ERROR("  [{}] {}", i, symbol);
        }
      } else {
        // No mangled symbol or unknown, print as-is
        ERROR("  [{}] {}", i, symbol);
      }
    } else {
      // No function info, print as-is
      ERROR("  [{}] {}", i, symbol);
    }
  }
  ERROR("==================");
  ERROR("Note: For file/line info, use: addr2line -e build/src/rtpmidid -f -C <address>");
  free(strings);
}

void sigabrt_f(int sig) {
  ERROR("SIGABRT received (signal {}) - printing stacktrace", sig);
  print_stacktrace();
  // Re-raise to get core dump
  signal(sig, SIG_DFL);
  raise(sig);
}

class main_t {
protected:
  std::shared_ptr<rtpmididns::midirouter_t> router;
  std::shared_ptr<rtpmididns::aseq_t> aseq;
  std::optional<rtpmididns::HwAutoAnnounce> hwautoannounce;
  rtpmididns::control_socket_t control;
  std::optional<rtpmididns::rtpmidi_remote_handler_t> rtpmidi_remote_handler;
  rtpmididns::web_server_t web;

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
    
    // Setup threading infrastructure
    setup_threading();

    web.router = router;
    web.aseq = aseq;
    web.mdns = rtpmididns::mdns;
    web.start();
  }
  
  void setup_threading() {
    // Set up router's peer enqueue function
    router->set_peer_enqueue_function([this](rtpmididns::peer_id_t peer_id, 
                                              const rtpmidid::midi_packet_t &packet) {
      auto peer = router->get_peer_by_id(peer_id);
      if (peer) {
        peer->enqueue_midi_packet(packet);
      }
    });
    
    // Start router thread
    router->start_router_thread();
    
    // Start all peer threads
    DEBUG("[MIDI_FLOW] Main: Starting all peer threads");
    router->for_each_peer(std::function<void(rtpmididns::midipeer_t*)>([](rtpmididns::midipeer_t *peer) {
      DEBUG("[MIDI_FLOW] Main: Starting thread for peer {}", peer->peer_id);
      peer->start_thread();
    }));
    DEBUG("[MIDI_FLOW] Main: All peer threads started");
  }

  void close() {
    INFO("Shutting down: stopping web server");
    web.stop();

    INFO("Shutting down: stopping DNS resolver");
    rtpmidid::dns_resolver_shutdown();

    INFO("Shutting down: stopping control socket");
    control.stop();

    INFO("Shutting down: dropping rtpmidi remote handler");
    rtpmidi_remote_handler.reset();

    INFO("Shutting down: dropping hw auto-announce");
    hwautoannounce.reset();

    if (router) {
      INFO("Shutting down: stopping peer threads");
      router->for_each_peer(
          std::function<void(rtpmididns::midipeer_t *)>([](rtpmididns::midipeer_t *peer) {
            INFO("Shutting down: stopping peer thread {}", peer->peer_id);
            peer->stop_thread();
          }));

      INFO("Shutting down: stopping router thread");
      router->stop_router_thread();

      INFO("Shutting down: removing all peers");
      router->remove_all_peers();
    }

    INFO("Shutting down: stopping mDNS");
    rtpmididns::mdns.reset();

    INFO("Shutting down: closing ALSA sequencer");
    aseq.reset();

    INFO("Shutting down: releasing router");
    router.reset();
  }

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
      create_rawmidi_rtpclient_pair(router.get(), rawmidi);
    }
  }
};

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  rtpmididns::parse_argv(std::move(args), &rtpmididns::settings);

  // Initialize logger level from settings with validation
  // LOG_LEVEL: 1=DEBUG, 2=INFO, 3=WARNING, 4=ERROR
  // enum: DEBUG=0, INFO=1, WARNING=2, ERROR=3
  constexpr int compile_time_min_enum = LOG_LEVEL - 1; // Convert to enum value
  int runtime_level = static_cast<int>(rtpmididns::settings.log_level);
  if (runtime_level < compile_time_min_enum) {
    WARNING("Requested log level {} is lower than compile-time minimum {}. "
            "Using minimum level {} instead.",
            rtpmididns::settings.log_level, LOG_LEVEL,
            static_cast<rtpmidid::logger_level_t>(compile_time_min_enum));
    rtpmidid::logger2.set_log_level(
        static_cast<rtpmidid::logger_level_t>(compile_time_min_enum));
  } else {
    rtpmidid::logger2.set_log_level(rtpmididns::settings.log_level);
  }

  // Worker threads must not handle SIGINT/SIGTERM; only main does.
  rtpmidid::block_shutdown_signals();

  struct sigaction sa_abrt {};
  sa_abrt.sa_handler = sigabrt_f;
  sigemptyset(&sa_abrt.sa_mask);
  sa_abrt.sa_flags = 0;
  (void)sigaction(SIGABRT, &sa_abrt, nullptr);
  signal(SIGPIPE, SIG_IGN);

  int shutdown_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (shutdown_eventfd < 0) {
    ERROR("Could not create shutdown eventfd: {}", strerror(errno));
    return 1;
  }

  rtpmidid::install_shutdown_signal_handlers(shutdown_eventfd);

  rtpmidid::poller_t::listener_t shutdown_listener;
  try {
    shutdown_listener = rtpmidid::poller.add_fd_in(shutdown_eventfd, [](int fd) {
      INFO("Shutdown signal received.");
      uint64_t n = 0;
      while (read(fd, &n, sizeof n) == static_cast<ssize_t>(sizeof n)) {
      }
      rtpmidid::poller.close();
    });
  } catch (const std::exception &e) {
    ERROR("Could not register shutdown eventfd: {}", e.what());
    ::close(shutdown_eventfd);
    return 1;
  }

  main_t maindata;

  // SETUP
  try {
    maindata.setup();
  } catch (const std::exception &exc) {
    ERROR("Error on setup: {}", exc.what());
    print_stacktrace();
    return 1;
  } catch (...) {
    ERROR("Unhandled exception in setup!");
    print_stacktrace();
    return 1;
  }

  // Deliver SIGINT/SIGTERM to the main thread only (workers inherit blocked mask).
  rtpmidid::unblock_shutdown_signals();

  // MAIN RUN
  try {
    INFO("Waiting for connections.");
    while (rtpmidid::poller.is_open()) {
      rtpmidid::poller.wait();
    }
  } catch (const std::exception &exc) {
    ERROR("Unhandled exception: {}!", exc.what());
    print_stacktrace();
  } catch (...) {
    ERROR("Unhandled exception!");
    print_stacktrace();
  }

  shutdown_listener.stop();
  if (shutdown_eventfd >= 0) {
    ::close(shutdown_eventfd);
    shutdown_eventfd = -1;
  }

  maindata.close();

  INFO("FIN");
  return 0;
}
