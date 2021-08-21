/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2021 David Moreno Montero <dmoreno@coralbits.com>
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

struct timer_event_t {
  std::chrono::steady_clock::time_point when;
  int id;
  std::function<void(void)> callback;
};

struct poller_private_data_t {
  int epollfd;
  std::map<int, std::function<void(int)>> fd_events;
  std::vector<timer_event_t> timer_events;
  std::vector<std::function<void(void)>> later_events;
  int max_timer_id = 1;
};

poller_t rtpmidid::poller;

static bool poller_initialized = false;

poller_t::poller_t() {
  if (poller_initialized) {
    throw exception("Poller already initialized. Can only use one poller "
                    "(rtpmidid::poller).");
  }
  poller_initialized = true;

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
  poller_initialized = false;
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

  using namespace std::chrono_literals;

  // We can not call directly as it might need to be called out of this call
  // stack
  if (ms.count() <= 0) {
    // DEBUG("Not added to timer list, but to call later list. {}ms",
    // ms.count());
    call_later(f);
    return poller_t::timer_t(0);
  }

  auto pd = static_cast<poller_private_data_t *>(private_data);

  // This 1ms is because of the precission mismatch later. now() is in
  // microseconds, and later we need also ms. This way we ensure ms precission.
  auto timer_id = pd->max_timer_id++;
  auto when = std::chrono::steady_clock::now() + ms + 1ms;

  pd->timer_events.push_back(timer_event_t{when, timer_id, f});
  std::sort(std::begin(pd->timer_events), std::end(pd->timer_events),
            [](const auto &a, const auto &b) { return a.when < b.when; });

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
  //  DEBUG("Remove {}. {}? {}", tid.id, b.id, b.id == tid.id);
  pd->timer_events.erase(
      std::remove_if(pd->timer_events.begin(), pd->timer_events.end(),
                     [&tid](const auto &b) { return b.id == tid.id; }),
      pd->timer_events.end());
  // DEBUG("Remove timer {}. {} left", tid.id, pd->timer_events.size());
  // Invalidate
  tid.id = 0;
}

static int chrono_ms_to_int(std::chrono::milliseconds &ms) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(ms).count();
}

static int ms_to_now(std::chrono::steady_clock::time_point &tp) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             tp - std::chrono::steady_clock::now())
      .count();
}

static void run_expired_timer_events(std::vector<timer_event_t> &events) {
  // if (events.size()) {
  //   DEBUG("Next event in {} ms", ms_to_now(events[0].when));
  // }

  while (events.size() > 0 && ms_to_now(events[0].when) <= 0) {
    // Will ensure remove via RTTI
    auto firstI = events.begin();
    poller_t::timer_t id(firstI->id);
    firstI->callback();

    // There is no need for this erase, as the operator= for the timer_t
    // ensures removal. This is this way to allow use of RTTI on more
    // complex items.
    // pd->timer_events.erase(firstI);
  }
}

void run_call_later_events(poller_private_data_t *pd) {
  while (!pd->later_events.empty()) {
    std::vector<std::function<void(void)>> call_now;
    // Clean the later, get the now.
    std::swap(call_now, pd->later_events);
    for (auto &f : call_now) {
      f();
    }
  }
}

void poller_t::wait(std::optional<std::chrono::milliseconds> max_wait_ms) {
  auto pd = static_cast<poller_private_data_t *>(private_data);

  const auto MAX_EVENTS = 10;
  struct epoll_event events[MAX_EVENTS];
  auto wait_ms = 10'000'000; // not forever, but a lot (10'000s)

  // Maybe some default value, set as max wait
  if (max_wait_ms.has_value()) {
    auto max_wait_in_ms = chrono_ms_to_int(max_wait_ms.value());
    wait_ms = max_wait_in_ms;
  }

  // How long should I wait at max
  if (!pd->timer_events.empty()) {
    wait_ms = ms_to_now(pd->timer_events[0].when);
    wait_ms = std::max(wait_ms, 0); // min wait 0ms.
  }
  // DEBUG("Wait {} ms", wait_ms);
  run_call_later_events(pd);

  // wait, get events or timeouts
  auto nfds = 0;
  if (wait_ms != 0) { // Maybe no wait. Some timer event pending.
    nfds = epoll_wait(pd->epollfd, events, MAX_EVENTS, wait_ms);

    if (nfds == -1)
      ERROR("epoll_wait failed: {}", strerror(errno));
  }

  // Run events
  for (auto n = 0; n < nfds; n++) {
    // DEBUG("IO EVENT");
    auto fd = events[n].data.fd;
    try {
      pd->fd_events[fd](fd);
    } catch (const std::exception &e) {
      ERROR("Caught exception at poller: {}", e.what());
    }
  }

  run_call_later_events(pd);
  run_expired_timer_events(pd->timer_events);
  run_call_later_events(pd);
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
