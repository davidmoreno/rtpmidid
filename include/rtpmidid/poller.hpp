/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
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
#include <chrono>
#include <ctime>
#include <functional>
#include <map>
#include <vector>

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

  poller_t();
  ~poller_t();

  // Call this function in X seconds
  timer_t add_timer_event(std::chrono::milliseconds ms,
                          std::function<void(void)> event_f);
  void remove_timer(timer_t &tid);

  // Just call it later. after finishing current round of event loop
  void call_later(std::function<void(void)> later_f);

  void add_fd_in(int fd, std::function<void(int)> event_f);
  void add_fd_out(int fd, std::function<void(int)> event_f);
  void add_fd_inout(int fd, std::function<void(int)> event_f);
  void remove_fd(int fd);

  void wait();

  void close();
  bool is_open();
};

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

// Singleton for all events on the system.
extern poller_t poller;
} // namespace rtpmidid
