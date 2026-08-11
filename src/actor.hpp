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

#pragma once

#include "mailbox.hpp"
#include "messages.hpp"
#include "rtpmidid/poller.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

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
  /// Owned mailbox; created by the actor when null.
  std::shared_ptr<actor_mailbox_t> mailbox;
  /// Where `stopped` / `actor_died` are posted (the owning actor: router
  /// for peers, main supervisor for top-level actors).
  mailbox_handle_t supervisor_mailbox;
};

class actor_t {
public:
  using mailbox_type = actor_mailbox_t;
  using data_type = data_message_t;
  using control_type = control_message_t;

  explicit actor_t(actor_config_t config);
  virtual ~actor_t();

  actor_t(const actor_t &) = delete;
  actor_t &operator=(const actor_t &) = delete;

  // --- lifecycle (2.4) ---

  /// Spawn the actor thread with its configured scheduling priority.
  void start();
  /// Graceful stop: posts the `stop` control message; the loop runs the
  /// stop hooks and exits.
  void request_stop();
  /// Escalation: raises the stop token (checked once per loop iteration).
  void request_stop_token();
  /// Threadless pump mode (D12): one loop pass with a zero wait timeout.
  /// Returns false when the loop has exited (and finalizes: stop hooks,
  /// `stopped`/`actor_died` posted, poller closed).
  bool pump();
  /// Finalize: run stop hooks, post `stopped` (or `actor_died` on fatal),
  /// close the poller. Called by the thread after the loop exits; tests
  /// call it after pumping (or rely on `pump()` auto-finalizing).
  void finish();

  // --- hooks ---

  /// Runs on the actor thread before the loop (failures are fatal).
  virtual void on_start() {}
  /// Runs on the actor thread when the loop exits.
  virtual void on_stop() {}
  /// Data-lane message handler (called on the actor thread only).
  virtual void on_data(data_message_t &&msg) { (void)msg; }
  /// Control-lane message handler (called on the actor thread only).
  virtual void on_control(control_message_t &&msg) { (void)msg; }
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
   * deadline-bounded. Sequential request/response code is straight-line:
   * send request, `wait_for` the response, repeat.
   */
  void wait_for(
      std::move_only_function<bool(const control_message_t &)> predicate,
      std::chrono::milliseconds deadline,
      std::move_only_function<void(std::optional<control_message_t>)> resume);

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

  const std::string &name() const { return config_.name; }
  actor_id_t id() const { return config_.id; }
  std::shared_ptr<mailbox_type> mailbox() { return mailbox_; }
  /// Move the owned thread out (delegated reap, D9): the router hands a
  /// wedged peer's jthread to the supervisor's reaper this way.
  std::jthread take_thread() { return std::move(thread_); }
  bool is_running() const { return thread_.joinable(); }
  bool is_stopping() const { return stopping_; }
  uint64_t message_exceptions() const { return message_exceptions_; }
  drain_policy_t drain_policy() const { return drain_policy_; }
  void set_drain_policy(drain_policy_t policy) { drain_policy_ = policy; }

  /**
   * One loop pass (D8 protocol): read the doorbell, drain per policy,
   * re-check emptiness before sleeping (no lost wakeup), service
   * fd/timer events. Returns false when the loop must exit.
   */
  bool run_once(std::optional<std::chrono::milliseconds> timeout = {});

protected:
  actor_config_t config_;
  std::shared_ptr<actor_mailbox_t> mailbox_;
  bool stopping_ = false;

private:
  void thread_main();
  void apply_scheduling();
  void drain_with_policy();
  void handle_data_safe(data_message_t &&msg);
  void handle_control_safe(control_message_t &&msg);
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
    std::move_only_function<bool(const control_message_t &)> predicate;
    std::move_only_function<void(std::optional<control_message_t>)> resume;
    std::chrono::steady_clock::time_point deadline;
  };
  waiter_t waiter_;

  // Declared after the poller: destroyed before it (member destruction is
  // in reverse declaration order), so the doorbell fd is removed from the
  // still-open poller.
  rtpmidid::poller_t::listener_t doorbell_listener_;
};

} // namespace rtpmididns
