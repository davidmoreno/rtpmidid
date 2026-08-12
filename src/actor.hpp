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

/// Actor runtime (spec: actor-runtime; design D1-D15).
///
/// An actor owns exactly one thread (`std::jthread`), one private
/// `poller_t` instance (epoll) and one mailbox; the only cross-thread API
/// is posting messages to its mailbox. The loop is structured as
/// `run_once(timeout)` so production threads call it forever and tests
/// drive it deterministically in threadless pump mode.
///
/// `actor_t<DataT, ControlT>` is templated on the actor's OWN accepted
/// message types: `ControlT` is the `std::variant` of control messages the
/// actor declares (its mailbox control lane element type), so an actor can
/// only ever receive — and its `on_control` only ever sees — the messages
/// it accepts (e.g. the mdns actor's variant contains no MIDI messages).

#pragma once

#include "mailbox.hpp"
#include "messages.hpp"
#include "rtpmidid/poller.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace rtpmididns {

/// Thread scheduling classes (design D10): data-plane actors (peers,
/// router) are elevated; the control socket and mdns run normal; the
/// worker runs idle.
enum class scheduling_class_t { normal, elevated, idle };

/// Pluggable drain policy (design D4): default data-first (drain data,
/// one control, repeat); FIFO for the worker.
enum class drain_policy_t { data_first, fifo };

struct actor_config_t {
  std::string name;
  scheduling_class_t scheduling = scheduling_class_t::normal;
  /// Real-time promotion (SCHED_FIFO) for elevated actors; opt-in via
  /// configuration. Requires RLIMIT_RTPRIO / CAP_SYS_NICE; on failure the
  /// actor falls back to nice-based elevation with a warning (D10).
  bool rt_enabled = false;
  int rt_priority = 10;
  actor_id_t id = 0;
  /// Where `stopped` / `actor_died` are posted (the owning actor: router
  /// for peers, main supervisor for top-level actors).
  mailbox_handle_t supervisor_mailbox;
};

/// The type-erased actor interface: lets heterogeneous actor lists
/// (supervisor-managed actors, spawned peers of any family) be handled
/// without knowing their lane types.
class actor_base_t {
public:
  virtual ~actor_base_t() = default;

  /// Spawn the actor thread with its configured scheduling priority.
  virtual void start() = 0;
  /// Graceful stop: posts the `stop` control message.
  virtual void request_stop() = 0;
  /// Escalation: raises the stop token (checked once per loop iteration).
  virtual void request_stop_token() = 0;
  /// One pump pass (threadless test mode).
  virtual bool pump() = 0;
  virtual void finish() = 0;
  virtual bool run_once(std::optional<std::chrono::milliseconds> timeout) = 0;
  /// Move the owned thread out (delegated reap, D9).
  virtual std::jthread take_thread() = 0;
  virtual bool is_running() const = 0;
  virtual bool is_stopping() const = 0;
  virtual const std::string &name() const = 0;
  virtual actor_id_t id() const = 0;
  virtual uint64_t message_exceptions() const = 0;
  virtual void set_drain_policy(drain_policy_t policy) = 0;
  virtual drain_policy_t drain_policy() const = 0;
  /// The actor's mailbox as a type-erased handle (cross-actor posting).
  virtual mailbox_handle_t mailbox_handle() = 0;
};

template <typename DataT, typename ControlT> class actor_t : public actor_base_t {
public:
  using mailbox_type = mailbox_t<DataT, ControlT>;
  using data_type = DataT;
  using control_type = ControlT;
  /// The actor's declared accepted control messages (per-actor variant).
  using control_messages = ControlT;

  explicit actor_t(actor_config_t config);
  ~actor_t() override;

  actor_t(const actor_t &) = delete;
  actor_t &operator=(const actor_t &) = delete;

  // --- lifecycle (2.4) ---

  /// Spawn the actor thread with its configured scheduling priority.
  void start() override;
  /// Graceful stop: posts the `stop` control message; the loop runs the
  /// stop hooks and exits.
  void request_stop() override;
  /// Escalation: raises the stop token (checked once per loop iteration).
  void request_stop_token() override;
  /// Threadless pump mode (D12): one loop pass with a zero wait timeout.
  bool pump() override;
  void finish() override;

  // --- hooks ---

  /// Runs on the actor thread before the loop (failures are fatal).
  virtual void on_start() {}
  /// Runs on the actor thread when the loop exits.
  virtual void on_stop() {}
  /// Data-lane message handler (called on the actor thread only).
  virtual void on_data(DataT &&msg) { (void)msg; }
  /// Control-lane message handler (called on the actor thread only); the
  /// message type is this actor's declared control variant.
  virtual void on_control(ControlT &&msg) { (void)msg; }
  /// Called once per loop pass after draining (busy actors use this for
  /// periodic work such as pending-table deadline checks).
  virtual void on_loop() {}

  // --- selective control wait (D15) ---

  /**
   * Parks control dispatch until the first queued control message matching
   * `predicate` arrives (consuming only that message; non-matching
   * messages stay queued in order) or the deadline expires, resuming with
   * the matched message or `std::nullopt` (timeout outcome). While parked
   * the data lane keeps being drained and fd/timer events keep being
   * serviced. At most one waiter is active per actor; every wait is
   * deadline-bounded.
   */
  void wait_for(std::move_only_function<bool(const ControlT &)> predicate,
                std::chrono::milliseconds deadline,
                std::move_only_function<void(std::optional<ControlT>)> resume);

  // --- actor-local fds and timers (2.1) ---

  rtpmidid::poller_t::listener_t add_fd_in(int fd, std::function<void(int)> f) {
    return poller_.add_fd_in(fd, std::move(f));
  }
  rtpmidid::poller_t::listener_t add_fd_out(int fd, std::function<void(int)> f) {
    return poller_.add_fd_out(fd, std::move(f));
  }
  rtpmidid::poller_t::listener_t add_fd_inout(int fd, std::function<void(int)> f) {
    return poller_.add_fd_inout(fd, std::move(f));
  }
  rtpmidid::poller_t::timer_t add_timer(std::chrono::milliseconds ms,
                                        std::function<void()> f) {
    return poller_.add_timer_event(ms, std::move(f));
  }

  // --- accessors ---

  const std::string &name() const override { return config_.name; }
  actor_id_t id() const override { return config_.id; }
  std::shared_ptr<mailbox_type> mailbox() { return mailbox_; }
  mailbox_handle_t mailbox_handle() override {
    return mailbox_handle_t{mailbox_};
  }
  /// Replace the mailbox before start/pump (e.g. a listener pre-creates a
  /// spawned peer's mailbox for routing). The doorbell is registered on
  /// the first loop pass with the final mailbox.
  void set_mailbox(std::shared_ptr<mailbox_type> mb) { mailbox_ = std::move(mb); }
  std::jthread take_thread() override { return std::move(thread_); }
  bool is_running() const override { return thread_.joinable(); }
  bool is_stopping() const override { return stopping_; }
  uint64_t message_exceptions() const override { return message_exceptions_; }
  drain_policy_t drain_policy() const override { return drain_policy_; }
  void set_drain_policy(drain_policy_t policy) override {
    drain_policy_ = policy;
  }

  /// The actor's own poller (its I/O surface: fds and timers are
  /// registered here, never in a global).
  rtpmidid::poller_t &poller() { return poller_; }

  /// One loop pass (D8 protocol): read the doorbell, drain per policy,
  /// re-check emptiness before sleeping (no lost wakeup), service
  /// fd/timer events. Returns false when the loop must exit.
  bool run_once(std::optional<std::chrono::milliseconds> timeout = {}) override;

protected:
  actor_config_t config_;
  std::shared_ptr<mailbox_type> mailbox_;
  bool stopping_ = false;

private:
  void thread_main();
  void apply_scheduling();
  void drain_with_policy();
  void handle_data_safe(DataT &&msg);
  void handle_control_safe(ControlT &&msg);
  void fatal(const std::string &reason);
  void post_exit_notice();

  rtpmidid::poller_t poller_;
  std::jthread thread_;
  std::stop_source stop_source_;
  drain_policy_t drain_policy_ = drain_policy_t::data_first;
  bool started_once_ = false;
  bool finished_ = false;
  std::string fatal_reason_;
  hdr_t stop_hdr_;
  uint64_t message_exceptions_ = 0;

  struct waiter_t {
    bool active = false;
    std::move_only_function<bool(const ControlT &)> predicate;
    std::move_only_function<void(std::optional<ControlT>)> resume;
    std::chrono::steady_clock::time_point deadline;
  };
  waiter_t waiter_;
  bool doorbell_registered_ = false;

  // Declared after the poller: destroyed before it (member destruction is
  // in reverse declaration order), so the doorbell fd is removed from the
  // still-open poller.
  rtpmidid::poller_t::listener_t doorbell_listener_;
};

// ---------------------------------------------------------------------------
// Implementation (template: header-only)
// ---------------------------------------------------------------------------

template <typename DataT, typename ControlT>
actor_t<DataT, ControlT>::actor_t(actor_config_t config)
    : config_(std::move(config)),
      mailbox_(std::make_shared<mailbox_type>()) {
  if (config_.supervisor_mailbox == nullptr) {
    WARNING("Actor {} created without a supervisor mailbox: stopped/actor_died "
            "will not be posted.",
            config_.name);
  }
}

template <typename DataT, typename ControlT>
actor_t<DataT, ControlT>::~actor_t() {
  if (thread_.joinable()) {
    // Wake the loop and escalate so the join cannot hang.
    mailbox_->post_control(make_stop());
    stop_source_.request_stop();
    thread_.request_stop();
    thread_.join();
  }
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::apply_scheduling() {
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

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::start() {
  if (thread_.joinable()) {
    WARNING("Actor {} already started.", config_.name);
    return;
  }
  thread_ = std::jthread([this] { thread_main(); });
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::request_stop() {
  mailbox_->post_control(make_stop());
}
template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::request_stop_token() {
  stop_source_.request_stop();
  if (thread_.joinable()) {
    thread_.request_stop();
  }
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::thread_main() {
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

template <typename DataT, typename ControlT>
bool actor_t<DataT, ControlT>::pump() {
  if (!started_once_) {
    started_once_ = true;
    try {
      on_start();
    } catch (const std::exception &e) {
      fatal(std::string("on_start: ") + e.what());
      finish();
      return false;
    } catch (...) {
      fatal("on_start: unknown exception");
      finish();
      return false;
    }
  }
  const bool cont = run_once(std::chrono::milliseconds(0));
  if (!cont) {
    finish();
  }
  return cont;
}

template <typename DataT, typename ControlT>
bool actor_t<DataT, ControlT>::run_once(
    std::optional<std::chrono::milliseconds> timeout) {
  if (!doorbell_registered_) {
    doorbell_registered_ = true;
    // The only fd the actor registers in its poller: the mailbox doorbell.
    doorbell_listener_ =
        poller_.add_fd_in(mailbox_->doorbell_fd(), [](int) {});
  }
  // Doorbell wake protocol (D8): read (reset) the doorbell and clear the
  // coalescing flag before draining.
  mailbox_->prepare();
  drain_with_policy();

  on_loop();

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
  // Stop-token check before sleeping: escalation latency is one iteration.
  if (stop_source_.stop_requested()) {
    return false;
  }
  poller_.wait(wait_ms);
  return !stopping_;
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::drain_with_policy() {
  if (waiter_.active) {
    // Parked (D15): scan the control lane for the first match, consume
    // only it; the data lane is always drained fully (MIDI unaffected).
    if (auto match = mailbox_->pop_matching(waiter_.predicate)) {
      auto resume = std::move(waiter_.resume);
      waiter_.active = false;
      resume(std::move(match));
    }
    mailbox_->drain_data(
        [this](DataT &&m) { handle_data_safe(std::move(m)); });
    return;
  }

  switch (drain_policy_) {
  case drain_policy_t::data_first:
    // D4: drain the data lane completely, then exactly one control
    // message, and repeat until the control lane is empty. Neither lane
    // starves; a control burst delays data by one message per pass.
    for (;;) {
      mailbox_->drain_data(
          [this](DataT &&m) { handle_data_safe(std::move(m)); });
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
        [this](DataT &&m) { handle_data_safe(std::move(m)); });
    break;
  }
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::handle_data_safe(DataT &&msg) {
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

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::handle_control_safe(ControlT &&msg) {
  if (std::holds_alternative<stop_t>(msg)) {
    stop_hdr_ = std::get<stop_t>(msg).hdr;
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

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::wait_for(
    std::move_only_function<bool(const ControlT &)> predicate,
    std::chrono::milliseconds deadline,
    std::move_only_function<void(std::optional<ControlT>)> resume) {
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

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::fatal(const std::string &reason) {
  fatal_reason_ = reason;
  stopping_ = true;
}

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::finish() {
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

template <typename DataT, typename ControlT>
void actor_t<DataT, ControlT>::post_exit_notice() {
  if (config_.supervisor_mailbox == nullptr) {
    return;
  }
  if (fatal_reason_.empty()) {
    config_.supervisor_mailbox.post_control(
        make_stopped(stop_hdr_, config_.id));
  } else {
    ERROR("Actor {} died: {}", config_.name, fatal_reason_);
    config_.supervisor_mailbox.post_control(
        make_actor_died(config_.id, fatal_reason_));
  }
}

} // namespace rtpmididns
