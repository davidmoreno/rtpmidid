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

#include "supervisor_actor.hpp"
#include "router_actor.hpp"
#include "rtpmidid/logger.hpp"
#include <csignal>
#include <cstring>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace rtpmididns {

supervisor_actor_t::supervisor_actor_t(actor_config_t config)
    : actor_t(std::move(config)) {}

void supervisor_actor_t::setup_signalfd() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
    ERROR("Supervisor: sigprocmask failed: {}", strerror(errno));
    return;
  }
  signalfd_ = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (signalfd_ < 0) {
    ERROR("Supervisor: signalfd failed: {}", strerror(errno));
    return;
  }
  signalfd_listener_ = add_fd_in(signalfd_, [this](int) {
    struct signalfd_siginfo info {};
    while (::read(signalfd_, &info, sizeof(info)) == sizeof(info)) {
      INFO("Supervisor: signal {} received; shutting down.", info.ssi_signo);
      request_shutdown();
    }
  });
}

void supervisor_actor_t::on_start() {
  // The reaper joins delegated jthreads off-loop (D9): shutdown never
  // hangs on a wedged thread.
  reaper_ = std::thread([this] { reaper_loop(); });
  setup_signalfd();
}

void supervisor_actor_t::request_shutdown() {
  if (stage_ != stage_t::none) {
    return;
  }
  INFO("Supervisor: ordered shutdown starting.");
  begin_shutdown();
}

void supervisor_actor_t::begin_shutdown() {
  stage_ = stage_t::stopping_control;
  advance_shutdown();
}

void supervisor_actor_t::advance_shutdown() {
  switch (stage_) {
  case stage_t::stopping_control:
    // 1. The control socket stops accepting commands first.
    if (control_listener_) {
      control_listener_->request_stop();
    }
    stage_ = stage_t::router_stop_all;
    // Fall through to the router phase after one pass (the control
    // listener join happens in finalize via the reaper if needed).
    [[fallthrough]];
  case stage_t::router_stop_all:
    // 2. The router stops all spawned peers (bounded) and acks.
    if (router_) {
      shutdown_corr_ = 0x5A17A11;
      shutdown_timer_ = add_timer(3s, [this] {
        WARNING("Supervisor: router stop-all deadline; proceeding.");
        stage_ = stage_t::stopping_router;
        advance_shutdown();
      });
      router_->mailbox()->post_control(
          stop_all_t{hdr_t{shutdown_corr_}, mailbox()});
      stage_ = stage_t::stopping_router;
      return;
    }
    stage_ = stage_t::stopping_router;
    [[fallthrough]];
  case stage_t::stopping_router:
    // 3. Stop the router itself and the managed top-level actors.
    shutdown_timer_.disable();
    if (router_) {
      router_->request_stop();
    }
    for (auto &a : managed_) {
      a->request_stop();
    }
    // Completion: the router posts `stopped` after its loop exits; managed
    // actors post `stopped` too. Track completion via a deadline.
    shutdown_timer_ = add_timer(3s, [this] {
      WARNING("Supervisor: managed-actor stop deadline; reaping stragglers.");
      finalize();
    });
    stage_ = stage_t::stopping_rest;
    return;
  case stage_t::stopping_rest:
    return;
  case stage_t::none:
  case stage_t::done:
    return;
  }
}

void supervisor_actor_t::stop_actor(actor_t &actor) { actor.request_stop(); }

void supervisor_actor_t::on_control(control_message_t &&msg) {
  std::visit(
      [this](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, actor_died_t>) {
          ERROR("Supervisor: actor id={} died: {}", m.id, m.reason);
        } else if constexpr (std::is_same_v<T, reap_actor_t>) {
          handle_reap_actor(std::move(m));
        } else if constexpr (std::is_same_v<T, ack_t>) {
          // Router stop-all completed (bounded per-peer waits inside).
          if (stage_ == stage_t::stopping_router && m.hdr.corr == shutdown_corr_) {
            shutdown_timer_.disable();
            stage_ = stage_t::stopping_rest;
            advance_shutdown();
          }
        } else if constexpr (std::is_same_v<T, stopped_t>) {
          // A managed actor exited: finalize once the router is done.
          if (stage_ == stage_t::stopping_rest) {
            finalize();
          }
        }
      },
      msg.v);
}

void supervisor_actor_t::handle_reap_actor(reap_actor_t &&m) {
  {
    std::lock_guard<std::mutex> lk(reap_mutex_);
    reap_list_.push_back(std::move(m.thread));
    reaped_count_++;
  }
  reap_cv_.notify_one();
  WARNING("Supervisor: {} actor(s) delegated to the reaper.", reaped_count_);
}

void supervisor_actor_t::reaper_loop() {
  std::unique_lock<std::mutex> lk(reap_mutex_);
  while (!reaper_stop_) {
    reap_cv_.wait(lk, [&] { return reaper_stop_ || !reap_list_.empty(); });
    while (!reap_list_.empty()) {
      auto t = std::move(reap_list_.back());
      reap_list_.pop_back();
      lk.unlock();
      if (t.joinable()) {
        t.join(); // off-loop: never inside a message loop
      }
      lk.lock();
    }
  }
}

void supervisor_actor_t::finalize() {
  if (shutdown_complete_) {
    return;
  }
  shutdown_complete_ = true;
  shutdown_timer_.disable();
  INFO("Supervisor: shutdown complete.");
  // The wrapper posts `stopped` to the configured supervisor mailbox
  // (main in the daemon) so the process can exit cleanly.
}

supervisor_actor_t::~supervisor_actor_t() {
  {
    std::lock_guard<std::mutex> lk(reap_mutex_);
    reaper_stop_ = true;
  }
  reap_cv_.notify_all();
  if (reaper_.joinable()) {
    reaper_.join();
  }
  std::lock_guard<std::mutex> lk(reap_mutex_);
  for (auto &t : reap_list_) {
    if (t.joinable()) {
      WARNING("Supervisor: detaching still-alive reaped thread at daemon "
              "exit.");
      t.detach();
    }
  }
  reap_list_.clear();
}

} // namespace rtpmididns
