/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
#include "logger_actor.hpp"
#include "mdns_actor.hpp"
#include "router_actor.hpp"
#include "rtpmidi_server_actor.hpp"
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include "supervisor_actor.hpp"
#include "worker_actor.hpp"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace rtpmididns {
// Defined in argv.cpp (RTPMIDID_VERSION).
extern const char *VERSION;

static void setup_static_peers(const std::shared_ptr<rtpmidi_server_actor_t> &server) {
  // Lazy outbound connections (settings.connect_to): each becomes a
  // waiting ALSA port on the listener plus an outbound target on the
  // rtpmidi server; the session starts only when an ALSA client
  // subscribes to the port (lazy-rtpmidi-connections, design D8).
  for (auto &ct : settings.connect_to) {
    server->mailbox()->post_control(server_connect_to_t{
        ct.name.empty() ? ct.hostname : ct.name, ct.hostname, ct.port,
        ct.local_udp_port});
  }

  // Rawmidi devices (settings.rawmidi): server-mode devices are registered
  // as exports without opening them (deferred open on connection);
  // client-mode devices (`hostname=` set) keep the eager open + outbound
  // connect (design D5).
  for (auto &rm : settings.rawmidi) {
    const auto port = rm.local_udp_port.empty()
                          ? uint16_t(0)
                          : uint16_t(std::stoul(rm.local_udp_port));
    if (rm.hostname.empty()) {
      server->mailbox()->post_control(
          export_add_t{hdr_t{0}, {},
                       rm.name.empty() ? rm.device : rm.name,
                       export_kind_e::rawmidi, rm.device, port});
    } else {
      server->mailbox()->post_control(server_rawmidi_client_t{
          hdr_t{0}, {}, rm.name.empty() ? rm.device : rm.name, rm.device,
          rm.hostname, rm.remote_udp_port, rm.local_udp_port});
    }
  }

  // [rtpmidi_announce] sections: generic "Network" servers on the rtpmidi
  // server (listen sockets + mDNS announcement; per-connection peer pairs
  // on accept).
  for (auto &ann : settings.rtpmidi_announce) {
    const auto port =
        ann.port.empty() ? uint16_t(0) : uint16_t(std::stoul(ann.port));
    server->mailbox()->post_control(export_add_t{
        hdr_t{0}, {}, ann.name, export_kind_e::network, "", port});
  }
}

} // namespace rtpmididns

namespace {

/// True when the environment variable is present and non-empty (the
/// NO_COLOR / FORCE_COLOR conventions treat any non-empty value as set).
bool env_var_nonempty(const char *name) {
  const char *v = ::getenv(name);
  return v != nullptr && v[0] != '\0';
}

/// Compute the process-global color flag once from the full precedence
/// chain and install it. Called before argument parsing (so early logs
/// honor env/tty) and again after (so --log-no-color and INI log_color
/// take effect).
void apply_log_color_config() {
  const auto &s = rtpmididns::settings;
  const bool ini_never = s.log_color == rtpmididns::log_color_t::never;
  const bool ini_always = s.log_color == rtpmididns::log_color_t::always;
  const bool color = rtpmidid::compute_log_color_enabled(
      s.log_no_color, env_var_nonempty("NO_COLOR"),
      env_var_nonempty("FORCE_COLOR"), ini_never, ini_always,
      ::isatty(STDOUT_FILENO));
  rtpmidid::set_log_color_enabled(color);
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char **argv) {
  // Main-thread identity: log tag only. The comm is deliberately left as
  // the process name (rtpmidid) — the main thread's comm IS the process
  // comm, and killall/pgrep -x/systemd match on it.
  rtpmidid::set_log_thread_tag("main");

  // Compute color from env + tty before parsing args, so the parse's own
  // DEBUG/INFO logs are colored correctly.
  apply_log_color_config();

  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }
  rtpmididns::parse_argv(std::move(args), &rtpmididns::settings);

  // Recompute now that --log-no-color and INI log_color are known.
  apply_log_color_config();

  // Initialize logger level from settings with validation.
  constexpr int compile_time_min_enum = LOG_LEVEL - 1;
  const int runtime_level = static_cast<int>(rtpmididns::settings.log_level);
  if (runtime_level < compile_time_min_enum) {
    WARNING("Requested log level requested={} is lower than compile-time "
            "minimum={}; using minimum={} instead.",
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
  INFO("Installing SIGTERM/SIGINT handlers eventfd={}", signal_fd_global);
  if (sigaction(SIGTERM, &sa, nullptr) != 0 ||
      sigaction(SIGINT, &sa, nullptr) != 0) {
    ERROR("Failed to install signal handlers error={}", quoted_t{strerror(errno)});
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

  // Logger actor: owns the daemon's stdout logging (INFO/WARNING/DEBUG/
  // ERROR route here through the global sink). Created right after the
  // supervisor so it can report to it; every log from here on (actor
  // construction, scheduling, wiring) goes through this actor.
  auto logger = std::make_shared<logger_actor_t>(
      actor_config_t{.name = "logger", .supervisor_mailbox = sup_mb});
  logger->install();

  auto router = std::make_shared<router_actor_t>(
      actor_config_t{.name = "router",
                     .scheduling = scheduling_class_t::elevated,
                     .rt_enabled = settings.rt_enable,
                     .rt_priority = settings.rt_priority,
                     .supervisor_mailbox = sup_mb});
  auto worker = std::make_shared<worker_actor_t>(
      actor_config_t{.name = "worker", .supervisor_mailbox = sup_mb});
  // The two global server actors (design D1): the ALSA listener owns the
  // seq client and all ports; the rtpmidi server owns every listen socket,
  // the export registry and the session bookkeeping. Neither is a router
  // peer. They use each other's mailboxes (subscription session requests,
  // port management), so the mailboxes are created explicitly and bound
  // before start.
  std::vector<std::string> alsa_ports;
  for (auto &ann : settings.alsa_announce) {
    alsa_ports.push_back(ann.name);
  }
  auto alsa_mb = std::make_shared<alsa_mailbox_t>();
  auto mdns_mb = std::make_shared<mdns_mailbox_t>();
  auto server_mb = std::make_shared<server_mailbox_t>();
  auto alsa = std::make_shared<alsa_actor_t>(
      actor_config_t{.name = "alsa", .supervisor_mailbox = sup_mb},
      settings.alsa_name, std::move(alsa_ports), router->mailbox(), server_mb);
  alsa->set_mailbox(alsa_mb);

  auto mdns = std::make_shared<mdns_actor_t>(
      actor_config_t{.name = "mdns", .supervisor_mailbox = sup_mb}, alsa_mb,
      server_mb);
  mdns->set_mailbox(mdns_mb);

  auto server = std::make_shared<rtpmidi_server_actor_t>(
      actor_config_t{.name = "rtpmidi_server", .supervisor_mailbox = sup_mb},
      router->mailbox(), alsa_mb, mdns_mb, worker);
  server->set_mailbox(server_mb);

  // Control socket: listener + one connection actor per client. Status
  // gathers router + mdns + exports (ALSA listener, rtpmidi server).
  auto control = std::make_shared<control_listener_actor_t>(
      actor_config_t{.name = "control", .supervisor_mailbox = sup_mb},
      settings.control, router->mailbox(), mdns->mailbox(), alsa_mb, server_mb,
      worker, VERSION);

  // Supervisor wiring: ordered shutdown control -> router -> the rest.
  supervisor->set_router(router);
  supervisor->set_control_listener(control);
  supervisor->add_managed(server);
  supervisor->add_managed(alsa);
  supervisor->add_managed(mdns);
  supervisor->add_managed(worker);

  // Start everything.
  try {
    logger->start();
    supervisor->start();
    router->start();
    worker->start();
    mdns->start();
    alsa->start();
    server->start();
    control->start();
  } catch (const std::exception &e) {
    ERROR("Fatal setup error error={}", quoted_t{e.what()});
    return 1;
  }

  // Static config (connect_to, rawmidi, announce sections) is registered
  // on the rtpmidi server; connections stay lazy until needed.
  setup_static_peers(server);

  INFO("rtpmidid version={} running (actor architecture).", quoted_t{VERSION});

  // The supervisor owns the lifecycle: SIGTERM/SIGINT arrive via its
  // signalfd and drive the ordered shutdown.
  while (!supervisor->shutdown_complete()) {
    std::this_thread::sleep_for(50ms);
  }

  // The logger is the last actor to stop: the stop control message is
  // processed only after the queued log lines are drained (data-first
  // policy), so the final INFO above and every shutdown log are flushed
  // before the process exits. Join the thread here — leaving it to the
  // destructor would race the graceful stop with the stop-token
  // escalation and could drop the last lines.
  INFO("Shutdown complete. Exiting.");
  logger->request_stop();
  auto logger_thread = logger->take_thread();
  logger_thread.join();
  return 0;
}
