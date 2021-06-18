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

#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/poller.hpp>

using namespace rtpmidid;

struct poller_private_data_t {
  int epollfd;
  std::map<int, std::function<void(int)>> fd_events;
  std::vector<std::tuple<std::chrono::steady_clock::time_point, int,
                         std::function<void(void)>>>
      timer_events;
  std::vector<std::function<void(void)>> later_events;
  int max_timer_id = 1;
};

poller_t rtpmidid::poller;

poller_t::poller_t() {
  poller_private_data_t *pd = new poller_private_data_t;
  pd->epollfd = epoll_create1(0);
  if (pd->epollfd < 0) {
    throw exception("Could not start epoll: {}", strerror(errno));
  }
  this->private_data = pd;
}
poller_t::~poller_t() {
  close();

  auto pd = static_cast<poller_private_data_t *>(private_data);
  delete pd;
}

bool poller_t::is_open() {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  return pd->epollfd > 0;
}

void poller_t::close() {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  if (pd->epollfd > 0) {
    ::close(pd->epollfd);
    pd->epollfd = -1;
  }
}

void poller_t::add_fd_inout(int fd, std::function<void(int)> f) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  pd->fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd = fd;
  auto r = epoll_ctl(pd->epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1) {
    throw exception("Can't add fd {} to poller: {} ({})", fd, strerror(errno),
                    errno);
  }
}
void poller_t::add_fd_in(int fd, std::function<void(int)> f) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  pd->fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLIN;
  ev.data.fd = fd;
  auto r = epoll_ctl(pd->epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1) {
    throw exception("Can't add fd {} to poller: {} ({}, ep {})", fd,
                    strerror(errno), errno);
  }
}
void poller_t::add_fd_out(int fd, std::function<void(int)> f) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  pd->fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLOUT;
  ev.data.fd = fd;
  auto r = epoll_ctl(pd->epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1) {
    throw exception("Can't add fd {} to poller: {} ({})", fd, strerror(errno),
                    errno);
  }
}

poller_t::timer_t poller_t::add_timer_event(std::chrono::milliseconds ms,
                                            std::function<void(void)> f) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  auto timer_id = pd->max_timer_id++;
  auto when = std::chrono::steady_clock::now() + ms;

  pd->timer_events.push_back(std::make_tuple(when, timer_id, f));
  std::sort(std::begin(pd->timer_events), std::end(pd->timer_events),
            [](const auto &a, const auto &b) {
              return std::get<0>(a) < std::get<0>(b);
            });

  // DEBUG("Added timer {}. {} s ({} pending)", timer_id, in_ms,
  // timer_events.size());
  return poller_t::timer_t(timer_id);
}

void poller_t::call_later(std::function<void(void)> later_f) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  pd->later_events.push_back(std::move(later_f));
}

void poller_t::remove_fd(int fd) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  pd->fd_events.erase(fd);
  if (is_open()) {
    auto r = epoll_ctl(pd->epollfd, EPOLL_CTL_DEL, fd, NULL);
    if (r == -1) {
      throw exception("Can't remove fd {} from poller: {} ({})", fd,
                      strerror(errno), errno);
    }
  }
}

void poller_t::remove_timer(timer_t &tid) {
  // already invalidated
  if (tid.id == 0) {
    return;
  }
  auto pd = static_cast<poller_private_data_t *>(private_data);
  pd->timer_events.erase(std::remove_if(pd->timer_events.begin(),
                                        pd->timer_events.end(),
                                        [&tid](const auto &b) {
                                          // DEBUG("Remove {}. {}? {}", tid.id,
                                          // std::get<1>(b), std::get<1>(b) ==
                                          // tid.id);
                                          return (std::get<1>(b) == tid.id);
                                        }),
                         pd->timer_events.end());
  // DEBUG("Remove timer {}. {} left", tid.id, timer_events.size());
  // Invalidate
  tid.id = 0;
}

void poller_t::wait() {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  const auto MAX_EVENTS = 32;
  struct epoll_event events[MAX_EVENTS];
  auto wait_ms = -1;

  if (!pd->timer_events.empty()) {
    auto now = std::chrono::steady_clock::now();
    wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::get<0>(pd->timer_events[0]) - now)
                  .count();
    // DEBUG("Timer event in {}ms", wait_ms);
    wait_ms = std::max(wait_ms, 0); // min wait 0ms.
  }

  auto nfds = 0;
  if (wait_ms != 0) // Maybe no wait. Some timer event pending.
    nfds = epoll_wait(pd->epollfd, events, MAX_EVENTS, wait_ms);

  for (auto n = 0; n < nfds; n++) {
    auto fd = events[n].data.fd;
    try {
      pd->fd_events[fd](fd);
    } catch (const std::exception &e) {
      ERROR("Catched exception at poller: {}", e.what());
    }
  }
  if (nfds == 0 && !pd->timer_events.empty()) { // This was a timeout
    // Will ensure remove via RTTI
    {
      auto firstI = pd->timer_events.begin();
      timer_t id = std::get<1>(*firstI);
      std::get<2> (*firstI)();

      // There is no need for this erase, as the operator= for the timer_t
      // ensures removal. This is this way to allow use of RTTI on more
      // complex items.
      // pd->timer_events.erase(firstI);
    }
    // if (!pd->timer_events.empty()) {
    //   DEBUG("Next timer event {}. {} left.",
    //   std::get<1>(pd->timer_events[0]),
    //         pd->timer_events.size());
    // } else {
    //   DEBUG("No more timer events.");
    // }
  }

  while (!pd->later_events.empty()) {
    std::vector<std::function<void(void)>> call_now;
    // Clean the later, get the now.
    std::swap(call_now, pd->later_events);
    for (auto &f : call_now) {
      f();
    }
  }
}

poller_t::timer_t::timer_t() : id(0) {}
poller_t::timer_t::timer_t(int id_) : id(id_) {}
poller_t::timer_t::timer_t(poller_t::timer_t &&other) {
  id = other.id;
  other.id = 0;
}
poller_t::timer_t::~timer_t() {
  if (id != 0) {
    poller.remove_timer(*this);
  }
}
poller_t::timer_t &poller_t::timer_t::operator=(poller_t::timer_t &&other) {
  if (id != 0) {
    poller.remove_timer(*this);
  }
  id = other.id;
  other.id = 0;

  return *this;
}

void poller_t::timer_t::disable() {
  poller.remove_timer(*this);
  id = 0;
}
