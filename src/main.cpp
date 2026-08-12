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

/// The daemon (task 8.1): main is the supervisor actor only. The global
/// poller singleton and the old single-threaded loop are gone from the
/// daemon; every concurrent unit (router, worker, mdns, ALSA, network
/// listeners, control socket, peers) is an actor owning its own thread,
/// poller and mailbox, communicating only through mailbox messages.

#include "actor.hpp"
#include "alsa_actor.hpp"
#include "argv.hpp"
#include "control_socket_actor.hpp"
#include "local_rawmidi_peer_actor.hpp"
#include "mdns_actor.hpp"
#include "network_rtpmidi_listener_actor.hpp"
#include "network_rtpmidi_peer_actor.hpp"
#include "router_actor.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include "supervisor_actor.hpp"
#include "worker_actor.hpp"
#include <chrono>
#include <csignal>
#include <memory>
#include <sys/eventfd.h>
#include <thread>

using namespace std::chrono_literals;

namespace rtpmididns {
// Defined in argv.cpp (RTPMIDID_VERSION).
extern const char *VERSION;

/// Spawn a standalone peer via the router (prepared bundle; the caller
/// does all fallible work before posting).
static void spawn_static_peer(const std::shared_ptr<router_actor_t> &router,
                              spawn_peer_t &&sp) {
  sp.reply_to = std::make_shared<reply_mailbox_t>();
  router->mailbox()->post_control(std::move(sp));
}

static void setup_static_peers(const std::shared_ptr<router_actor_t> &router,
                               const std::shared_ptr<worker_actor_t> &worker) {
  // Static outbound connections (settings.connect_to): each spawns a
  // network rtpmidi client peer; DNS runs on the worker.
  for (auto &ct : settings.connect_to) {
    spawn_peer_t sp;
    sp.type = "network_rtpmidi_peer_t";
    sp.meta = ct.name;
    sp.factory =
        [worker, ct](const mailbox_handle_t &sup, peer_id_t pid) {
          return std::make_shared<network_rtpmidi_peer_actor_t>(
              actor_config_t{.name = ct.name.empty() ? ct.hostname : ct.name,
                             .scheduling = scheduling_class_t::elevated,
                             .rt_enabled = settings.rt_enable,
                             .rt_priority = settings.rt_priority,
                             .id = pid,
                             .supervisor_mailbox = sup},
              ct.hostname, ct.port, ct.local_udp_port, worker);
        };
    spawn_static_peer(router, std::move(sp));
  }

  // Rawmidi devices (settings.rawmidi): each spawns a rawmidi peer actor.
  // The fd is opened here (preparation); on failure nothing is spawned.
  for (auto &rm : settings.rawmidi) {
    spawn_peer_t sp;
    sp.type = "local_rawmidi_peer_t";
    sp.meta = rm.name;
    sp.factory = [rm](const mailbox_handle_t &sup, peer_id_t pid) {
      const int fd = ::open(rm.device.c_str(), O_RDWR | O_NONBLOCK);
      if (fd < 0) {
        throw std::runtime_error("cannot open rawmidi device " + rm.device);
      }
      return std::make_shared<local_rawmidi_peer_actor_t>(
          actor_config_t{.name = rm.name.empty() ? rm.device : rm.name,
                         .scheduling = scheduling_class_t::elevated,
                         .rt_enabled = settings.rt_enable,
                         .rt_priority = settings.rt_priority,
                         .id = pid,
                         .supervisor_mailbox = sup},
          rm.device, rm.name, fd);
    };
    spawn_static_peer(router, std::move(sp));
  }
}

} // namespace rtpmididns

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }
  rtpmididns::parse_argv(std::move(args), &rtpmididns::settings);

  // Initialize logger level from settings with validation.
  constexpr int compile_time_min_enum = LOG_LEVEL - 1;
  const int runtime_level = static_cast<int>(rtpmididns::settings.log_level);
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

  // SIGTERM/SIGINT are handled via a process-wide handler writing to an
  // eventfd the supervisor polls (signalfd's mask-based wakeup is defeated
  // by the avahi/ALSA libraries resetting the signal mask).
  static int signal_fd_global = -1;
  signal_fd_global = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  struct sigaction sa {};
  sa.sa_handler = [](int) {
    const uint64_t one = 1;
    [[maybe_unused]] const ssize_t r =
        ::write(signal_fd_global, &one, sizeof(one));
  };
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  INFO("Installing SIGTERM/SIGINT handlers (eventfd {})", signal_fd_global);
  if (sigaction(SIGTERM, &sa, nullptr) != 0 ||
      sigaction(SIGINT, &sa, nullptr) != 0) {
    ERROR("Failed to install signal handlers: {}", strerror(errno));
    return 1;
  }
  const int signal_fd = signal_fd_global;

  // -------------------------------------------------------------------------
  // The actor graph. Every actor's supervisor mailbox is the supervisor's
  // own mailbox, so stopped/actor_died/reap are collected there.
  // -------------------------------------------------------------------------
  using namespace rtpmididns;

  auto supervisor = std::make_shared<supervisor_actor_t>(
      actor_config_t{.name = "supervisor"});
  supervisor->set_signal_fd(signal_fd);
  const auto sup_mb = supervisor->mailbox();

  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router",
                     .scheduling = scheduling_class_t::elevated,
                     .rt_enabled = settings.rt_enable,
                     .rt_priority = settings.rt_priority,
                     .supervisor_mailbox = sup_mb});
  auto worker = std::make_shared<worker_actor_t>(
      actor_config_t{.name = "worker", .supervisor_mailbox = sup_mb});
  // ALSA actor: one actor, many ports (announced ports registered as
  // hosted peer ids). The alsa and mdns actors use each other's mailboxes
  // (discovery requests ports; ALSA subscriptions initiate sessions), so
  // the mailboxes are created explicitly and bound before start.
  std::vector<std::string> alsa_ports;
  for (auto &ann : settings.alsa_announce) {
    alsa_ports.push_back(ann.name);
  }
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto mdns_mb = std::make_shared<mdns_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = sup_mb},
      settings.alsa_name, std::move(alsa_ports), router->mailbox(), mdns_mb);
  alsa->set_mailbox(alsa_mb);

  auto mdns = std::make_shared<mdns_actor_t>(
      actor_config_t{.name = "mdns", .supervisor_mailbox = sup_mb},
      router->mailbox(), alsa_mb, worker);
  mdns->set_mailbox(mdns_mb);

  // Network rtpmidi listeners (accept sockets in their own pollers).
  std::vector<std::shared_ptr<actor_base_t>> listeners;
  for (auto &ann : settings.rtpmidi_announce) {
    const auto port = ann.port.empty() ? uint16_t(0) : uint16_t(std::stoul(ann.port));
    listeners.push_back(std::make_shared<network_rtpmidi_listener_actor_t>(
        actor_config_t{.name = ann.name, .supervisor_mailbox = sup_mb},
        ann.name, port, router->mailbox(), alsa->mailbox()));
  }

  // Control socket: listener + one connection actor per client.
  auto control = std::make_shared<control_listener_actor_t>(
      actor_config_t{.name = "control", .supervisor_mailbox = sup_mb},
      settings.control, router->mailbox(), mdns->mailbox(), worker, VERSION);

  // Supervisor wiring: ordered shutdown control -> router -> the rest.
  supervisor->set_router(router);
  supervisor->set_control_listener(control);
  for (auto &l : listeners) {
    supervisor->add_managed(l);
  }
  supervisor->add_managed(alsa);
  supervisor->add_managed(mdns);
  supervisor->add_managed(worker);

  // Start everything.
  try {
    supervisor->start();
    router->start();
    worker->start();
    mdns->start();
    alsa->start();
    for (auto &l : listeners) {
      l->start();
    }
    control->start();
  } catch (const std::exception &e) {
    ERROR("Fatal setup error: {}", e.what());
    return 1;
  }

  // Static peers (connect_to clients, rawmidi) spawn via the router.
  setup_static_peers(router, worker);

  INFO("rtpmidid {} running (actor architecture).", VERSION);

  // The supervisor owns the lifecycle: SIGTERM/SIGINT arrive via its
  // signalfd and drive the ordered shutdown.
  while (!supervisor->shutdown_complete()) {
    std::this_thread::sleep_for(50ms);
  }

  INFO("Shutdown complete. Exiting.");
  return 0;
}
