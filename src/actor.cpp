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

#include "actor.hpp"
#include "rtpmidid/logger.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

namespace rtpmididns {

actor_t::actor_t(actor_config_t config)
    : config_(std::move(config)),
      mailbox_(config_.mailbox ? std::move(config_.mailbox)
                               : std::make_shared<actor_mailbox_t>()) {
  // The only fd the actor registers in its poller: the mailbox doorbell.
  doorbell_listener_ =
      poller_.add_fd_in(mailbox_->doorbell_fd(), [](int) {});
  if (config_.supervisor_mailbox == nullptr) {
    WARNING("Actor {} created without a supervisor mailbox: stopped/actor_died "
            "will not be posted.",
            config_.name);
  }
}

actor_t::~actor_t() {
  if (thread_.joinable()) {
    // Wake the loop and escalate so the join cannot hang.
    mailbox_->post_control(make_stop());
    stop_source_.request_stop();
    thread_.request_stop();
    thread_.join();
  }
}

void actor_t::apply_scheduling() {
  switch (config_.scheduling) {
  case scheduling_class_t::normal:
    break;
  case scheduling_class_t::elevated: {
    if (config_.rt_enabled) {
      struct sched_param param {};
      param.sched_priority = config_.rt_priority;
      int r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
      if (r == 0) {
        INFO("Actor {}: promoted to SCHED_FIFO priority {}", config_.name,
             config_.rt_priority);
      } else {
        WARNING("Actor {}: RT promotion to SCHED_FIFO failed ({}). Falling "
                "back to nice-based elevation.",
                config_.name, strerror(r));
        setpriority(PRIO_PROCESS, 0, -10);
      }
    } else {
      setpriority(PRIO_PROCESS, 0, -10);
    }
    break;
  }
  case scheduling_class_t::idle: {
    struct sched_param param {};
    param.sched_priority = 0;
    int r = pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);
    if (r == 0) {
      INFO("Actor {}: running at SCHED_IDLE.", config_.name);
    } else {
      WARNING("Actor {}: SCHED_IDLE unavailable ({}). Falling back to "
              "nice(19).",
              config_.name, strerror(r));
      setpriority(PRIO_PROCESS, 0, 19);
    }
    break;
  }
  }
}

void actor_t::start() {
  if (thread_.joinable()) {
    WARNING("Actor {} already started.", config_.name);
    return;
  }
  thread_ = std::jthread([this] { thread_main(); });
}

void actor_t::request_stop() { mailbox_->post_control(make_stop()); }
void actor_t::request_stop_token() {
  stop_source_.request_stop();
  if (thread_.joinable()) {
    thread_.request_stop();
  }
}

void actor_t::thread_main() {
  apply_scheduling();
  try {
    on_start();
  } catch (const std::exception &e) {
    fatal(std::string("on_start: ") + e.what());
    finish();
    return;
  } catch (...) {
    fatal("on_start: unknown exception");
    finish();
    return;
  }

  while (!stopping_ && !stop_source_.stop_requested()) {
    bool cont = false;
    try {
      cont = run_once({});
    } catch (const std::exception &e) {
      fatal(e.what());
      break;
    } catch (...) {
      fatal("unknown exception");
      break;
    }
    if (!cont) {
      break;
    }
  }
  finish();
}

bool actor_t::pump() {
  const bool cont = run_once(std::chrono::milliseconds(0));
  if (!cont) {
    finish();
  }
  return cont;
}

bool actor_t::run_once(std::optional<std::chrono::milliseconds> timeout) {
  // Doorbell wake protocol (D8): read (reset) the doorbell and clear the
  // coalescing flag before draining.
  mailbox_->prepare();
  drain_with_policy();

  if (stopping_ || stop_source_.stop_requested()) {
    return false;
  }

  // Selective waiter: enforce the deadline; a late match resumes the
  // waiter with a local timeout outcome.
  if (waiter_.active &&
      std::chrono::steady_clock::now() >= waiter_.deadline) {
    auto resume = std::move(waiter_.resume);
    waiter_.active = false;
    resume(std::nullopt);
  }

  // No-lost-wakeup re-check: if a push raced the reset, drain again
  // instead of sleeping. A parked waiter never sleeps with data pending.
  if (!mailbox_->idle() && !waiter_.active) {
    return true;
  }

  // Bounded wait: waiter deadline, timers, and the caller's timeout.
  std::optional<std::chrono::milliseconds> wait_ms = timeout;
  if (waiter_.active) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        waiter_.deadline - std::chrono::steady_clock::now());
    remaining = std::max(remaining, std::chrono::milliseconds(0));
    wait_ms = wait_ms.has_value() ? std::min(*wait_ms, remaining) : remaining;
  }
  if (!mailbox_->idle()) {
    wait_ms = std::chrono::milliseconds(0);
  }
  poller_.wait(wait_ms);
  return !stopping_;
}

void actor_t::drain_with_policy() {
  if (waiter_.active) {
    // Parked (D15): scan the control lane for the first match, consume
    // only it; the data lane is always drained fully (MIDI unaffected).
    if (auto match = mailbox_->pop_matching(waiter_.predicate)) {
      auto resume = std::move(waiter_.resume);
      waiter_.active = false;
      resume(std::move(match));
    }
    mailbox_->drain_data(
        [this](data_message_t &&m) { handle_data_safe(std::move(m)); });
    return;
  }

  switch (drain_policy_) {
  case drain_policy_t::data_first:
    // D4: drain the data lane completely, then exactly one control
    // message, and repeat until the control lane is empty. Neither lane
    // starves; a control burst delays data by one message per pass.
    for (;;) {
      mailbox_->drain_data(
          [this](data_message_t &&m) { handle_data_safe(std::move(m)); });
      if (waiter_.active || stopping_) {
        return; // a handler parked (or stop arrived): control dispatch is gated
      }
      auto c = mailbox_->pop_control();
      if (!c) {
        return;
      }
      handle_control_safe(std::move(*c));
    }
    break;
  case drain_policy_t::fifo:
    // Plain FIFO (worker): control lane first, then data.
    for (;;) {
      auto c = mailbox_->pop_control();
      if (!c) {
        break;
      }
      handle_control_safe(std::move(*c));
      if (waiter_.active || stopping_) {
        return;
      }
    }
    mailbox_->drain_data(
        [this](data_message_t &&m) { handle_data_safe(std::move(m)); });
    break;
  }
}

void actor_t::handle_data_safe(data_message_t &&msg) {
  try {
    on_data(std::move(msg));
  } catch (const std::exception &e) {
    message_exceptions_++;
    ERROR("Actor {}: exception handling data message: {}", config_.name,
          e.what());
  } catch (...) {
    message_exceptions_++;
    ERROR("Actor {}: unknown exception handling data message", config_.name);
  }
}

void actor_t::handle_control_safe(control_message_t &&msg) {
  if (std::holds_alternative<stop_t>(msg.v)) {
    stop_hdr_ = std::get<stop_t>(msg.v).hdr;
    stopping_ = true;
    return;
  }
  try {
    on_control(std::move(msg));
  } catch (const std::exception &e) {
    message_exceptions_++;
    ERROR("Actor {}: exception handling control message: {}", config_.name,
          e.what());
  } catch (...) {
    message_exceptions_++;
    ERROR("Actor {}: unknown exception handling control message", config_.name);
  }
}

void actor_t::wait_for(
    std::move_only_function<bool(const control_message_t &)> predicate,
    std::chrono::milliseconds deadline,
    std::move_only_function<void(std::optional<control_message_t>)> resume) {
  // The response may already be queued: consume it immediately.
  if (auto match = mailbox_->pop_matching(predicate)) {
    resume(std::move(match));
    return;
  }
  waiter_.active = true;
  waiter_.predicate = std::move(predicate);
  waiter_.resume = std::move(resume);
  waiter_.deadline = std::chrono::steady_clock::now() + deadline;
}

void actor_t::fatal(const std::string &reason) {
  fatal_reason_ = reason;
  stopping_ = true;
}

void actor_t::finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  try {
    on_stop();
  } catch (const std::exception &e) {
    ERROR("Actor {}: exception in on_stop: {}", config_.name, e.what());
  } catch (...) {
    ERROR("Actor {}: unknown exception in on_stop", config_.name);
  }
  poller_.close();
  post_exit_notice();
}

void actor_t::post_exit_notice() {
  if (config_.supervisor_mailbox == nullptr) {
    return;
  }
  if (fatal_reason_.empty()) {
    config_.supervisor_mailbox->post_control(make_stopped(stop_hdr_));
  } else {
    ERROR("Actor {} died: {}", config_.name, fatal_reason_);
    config_.supervisor_mailbox->post_control(
        make_actor_died(config_.id, fatal_reason_));
  }
}

} // namespace rtpmididns
