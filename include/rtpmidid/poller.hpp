/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once
#include "logger.hpp"
#include "utils.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

// #define DEBUG0 DEBUG
#define DEBUG0(...)

namespace rtpmidid {
struct poller_private_data_t;
/**
 * Simplified fd poller
 *
 * Internally uses epoll, it is level triggered, so data must be read or
 * will retrigger.
 */
class poller_t {
  NON_COPYABLE_NOR_MOVABLE(poller_t)
  std::unique_ptr<poller_private_data_t> private_data;

public:
  class timer_t;
  class listener_t;

  poller_t();
  ~poller_t();

  // Call this function in X seconds
  [[nodiscard]] timer_t add_timer_event(std::chrono::milliseconds ms,
                                        std::function<void(void)> event_f);
  void remove_timer(timer_t &tid);
  void clear_timers(); // This is used for destruction, and also for cleanup at
                       // tests.

  // Just call it later. after finishing current round of event loop
  void call_later(std::function<void(void)> later_f);

  [[nodiscard]] listener_t add_fd_in(int fd, std::function<void(int)> event_f);
  [[nodiscard]] listener_t add_fd_out(int fd, std::function<void(int)> event_f);
  [[nodiscard]] listener_t add_fd_inout(int fd,
                                        std::function<void(int)> event_f);

  // NOLINTNEXTLINE
  void __remove_fd(int fd);

  void wait(std::optional<std::chrono::milliseconds> wait_ms = {});

  void close();
  bool is_open();
};
// Singleton for all events on the system.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern poller_t poller;
// Every actor owns its own poller_t instance (see src/actor.hpp); the
// global above is removed at cutover (task 8.1).

class poller_t::timer_t {
  NON_COPYABLE(timer_t)
public:
  int id;
  poller_t *owner;

  timer_t() : id(0), owner(nullptr){};
  timer_t(poller_t *owner_, int id_);
  timer_t(timer_t &&) noexcept;
  ~timer_t();
  timer_t &operator=(timer_t &&other) noexcept;
  void disable();
};

class poller_t::listener_t {
  NON_COPYABLE(listener_t)
public:
  int fd = -1;
  poller_t *owner = nullptr;

  listener_t(poller_t *owner_, int fd_)
      : fd(fd_), owner(owner_) {
    DEBUG0("Create from fd {} with owner", fd);
  };
  listener_t() { DEBUG0("Create without fd"); };
  listener_t(listener_t &&other) noexcept : fd(other.fd), owner(other.owner) {
    DEBUG0("Create from other {}", other.fd);
    other.fd = -1;
    other.owner = nullptr;
  };
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~listener_t() {
    if (fd >= 0 && owner != nullptr)
      owner->__remove_fd(fd);
  }

  // NOLINTNEXTLINE(bugprone-exception-escape)
  listener_t &operator=(listener_t &&other) noexcept {
    if (fd >= 0 && owner != nullptr)
      owner->__remove_fd(fd);
    fd = other.fd;
    owner = other.owner;
    other.fd = -1;
    other.owner = nullptr;
    return *this;
  }
  void stop() {
    if (fd >= 0 && owner != nullptr)
      owner->__remove_fd(fd);
    fd = -1;
    owner = nullptr;
  }

  explicit operator bool() const { return fd >= 0; }
};
#undef DEBUG0
} // namespace rtpmidid
