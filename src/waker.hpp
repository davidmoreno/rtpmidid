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

#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>

namespace rtpmididns {

/**
 * Wake source: the signal mechanism between queue producers and the
 * (possibly sleeping) consumer.
 *
 * `wake()` is producer-side: it may be called from any thread, is
 * non-blocking and safe concurrently. `prepare()` is consumer-side and
 * consumes the pending signal before draining.
 *
 * The v1 implementation (`eventfd_waker_t`) wraps one eventfd plus an
 * atomic coalescing flag, so a burst of enqueues costs exactly one
 * eventfd write while the consumer sleeps (design D14).
 */
class waker_t {
public:
  virtual ~waker_t() = default;

  /// Producer side; any thread, coalescing-safe, never blocks.
  virtual void wake() noexcept = 0;
  /// Consumer side; consume the pending signal (reset before draining).
  virtual void prepare() noexcept = 0;
  /// The fd the actor registers as its doorbell.
  virtual int fd() const noexcept = 0;
};

class eventfd_waker_t final : public waker_t {
public:
  eventfd_waker_t() : fd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    if (fd_ < 0) {
      throw std::runtime_error("eventfd() failed");
    }
  }
  ~eventfd_waker_t() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  eventfd_waker_t(const eventfd_waker_t &) = delete;
  eventfd_waker_t &operator=(const eventfd_waker_t &) = delete;
  eventfd_waker_t(eventfd_waker_t &&other) noexcept
      : fd_(other.fd_), signaled_(other.signaled_.load()) {
    other.fd_ = -1;
  }
  eventfd_waker_t &operator=(eventfd_waker_t &&other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        close(fd_);
      }
      fd_ = other.fd_;
      signaled_.store(other.signaled_.load());
      other.fd_ = -1;
    }
    return *this;
  }

  /// Producer side: signal the consumer. Coalesces: while the flag is set
  /// no further eventfd writes happen, so a burst costs one write.
  void wake() noexcept override {
    if (!signaled_.exchange(true)) {
      const uint64_t one = 1;
      // EFD_NONBLOCK: never blocks. On failure (fd closed etc.) the
      // consumer's post-drain re-check catches the enqueued element.
      [[maybe_unused]] ssize_t r = ::write(fd_, &one, sizeof(one));
    }
  }

  /// Consumer side: consume the pending signal. Read the counter first,
  /// then clear the flag: a wake racing the clear either leaves the
  /// counter armed (poller returns immediately) or its element is
  /// observed by the re-check. No wakeup is ever lost.
  void prepare() noexcept override {
    uint64_t value = 0;
    [[maybe_unused]] ssize_t r = ::read(fd_, &value, sizeof(value));
    signaled_.store(false);
  }

  int fd() const noexcept override { return fd_; }

private:
  int fd_ = -1;
  std::atomic<bool> signaled_{false};
};

} // namespace rtpmididns
