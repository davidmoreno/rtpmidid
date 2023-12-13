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
#include <chrono>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// #define DEBUG0 DEBUG
#define DEBUG0(...)

namespace rtpmidid {
/**
 * Simplified fd poller
 *
 * Internally uses epoll, it is level triggered, so data must be read or
 * will retrigger.
 */
class poller_t {
  void *private_data;

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
  void __remove_fd(int fd);

  void wait(std::optional<std::chrono::milliseconds> wait_ms = {});

  void close();
  bool is_open();
};
// Singleton for all events on the system.
extern poller_t poller;

class poller_t::timer_t {
public:
  int id;

  timer_t();
  timer_t(int id_);
  timer_t(timer_t &&);
  ~timer_t();
  timer_t &operator=(timer_t &&other);
  void disable();

  // No copying
  timer_t(const timer_t &) = delete;
};

class poller_t::listener_t {
public:
  int fd = -1;

  listener_t(int fd_) : fd(fd_) { DEBUG0("Create from fd {}", fd); };
  listener_t() : fd(-1) { DEBUG0("Create without fd {}", fd); };
  listener_t(listener_t &&other) {
    DEBUG0("Create from other {}", other.fd);
    fd = other.fd;
    other.fd = -1;
  };
  ~listener_t() {
    if (fd >= 0)
      poller.__remove_fd(fd);
  }

  listener_t &operator=(listener_t &other) = delete;
  listener_t &operator=(const listener_t &other) = delete;
  listener_t &operator=(listener_t &&other) {
    if (fd >= 0)
      poller.__remove_fd(fd);
    fd = other.fd;
    other.fd = -1;
    return *this;
  }
  void stop() {
    if (fd >= 0)
      poller.__remove_fd(fd);
    fd = -1;
  }
};
#undef DEBUG0
} // namespace rtpmidid
