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

/// Main supervisor actor (tasks 6.4, 6.5; design D8/D9/D13): owns a
/// signalfd for SIGTERM/SIGINT (replacing raw signal handlers), collects
/// `actor_died`, joins reaped jthreads on a dedicated background reaper
/// thread (off-loop — the router never blocks and shutdown never hangs),
/// and drives the ordered shutdown: control -> router (stop-all) ->
/// ALSA/worker/mdns, with per-actor stop deadlines and reap fallback.
/// At daemon exit, still-alive reaped threads are detached with a warning.

#pragma once

#include "actor.hpp"
#include "messages.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace rtpmididns {

class router_actor_t;

class supervisor_actor_t : public actor_t {
public:
  explicit supervisor_actor_t(actor_config_t config);
  ~supervisor_actor_t() override;

  /// Register a top-level actor for shutdown management.
  void add_managed(std::shared_ptr<actor_t> actor) {
    managed_.push_back(std::move(actor));
  }
  void set_router(std::shared_ptr<router_actor_t> router) {
    router_ = std::move(router);
  }
  /// The actor that owns the control listening socket (stopped first).
  void set_control_listener(std::shared_ptr<actor_t> control) {
    control_listener_ = std::move(control);
  }

  /// Register the process-wide signal eventfd (written by the SIGTERM/
  /// SIGINT handler) in this actor's poller.
  void set_signal_fd(int fd) { signal_fd_ = fd; }

  /// Trigger the ordered shutdown (called from the signal eventfd or
  /// externally).
  void request_shutdown();
  bool shutdown_complete() const { return shutdown_complete_; }

  void on_start() override;
  void on_control(control_message_t &&msg) override;

private:
  enum class stage_t {
    none,
    stopping_control,
    router_stop_all,
    stopping_router,
    stopping_rest,
    done,
  };

  void setup_signalfd();
  void begin_shutdown();
  void advance_shutdown();
  void stop_actor(actor_t &actor);
  void handle_reap_actor(reap_actor_t &&m);
  void reaper_loop();
  void finalize();

  int signal_fd_ = -1;
  rtpmidid::poller_t::listener_t signal_fd_listener_;
  std::shared_ptr<router_actor_t> router_;
  std::shared_ptr<actor_t> control_listener_;
  std::vector<std::shared_ptr<actor_t>> managed_;

  stage_t stage_ = stage_t::none;
  bool shutdown_complete_ = false;
  uint64_t shutdown_corr_ = 0;
  rtpmidid::poller_t::timer_t shutdown_timer_;

  // reaper thread
  std::thread reaper_;
  std::mutex reap_mutex_;
  std::condition_variable reap_cv_;
  std::vector<std::jthread> reap_list_;
  bool reaper_stop_ = false;
  size_t reaped_count_ = 0;
};

} // namespace rtpmididns
