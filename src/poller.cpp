/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019 David Moreno Montero <dmoreno@coralbits.com>
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

#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>

#include "./exceptions.hpp"
#include "./poller.hpp"
#include "./logger.hpp"

using namespace rtpmidid;

poller_t rtpmidid::poller;

poller_t::poller_t(){
  epollfd = epoll_create1(0);
  if (epollfd < 0){
    throw exception("Could not start epoll: {}", strerror(errno));
  }
}
poller_t::~poller_t(){
  close();
}

void poller_t::close(){
  if (epollfd > 0){
    ::close(epollfd);
    epollfd = -1;
  }
}


void poller_t::add_fd_inout(int fd, std::function<void(int)> f){
  fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {} ({})", fd, strerror(errno), errno);
  }
}
void poller_t::add_fd_in(int fd, std::function<void(int)> f){
  fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLIN;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {} ({})", fd, strerror(errno), errno);
  }
}
void poller_t::add_fd_out(int fd, std::function<void(int)> f){
  fd_events[fd] = f;
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  ev.events = EPOLLOUT;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {} ({})", fd, strerror(errno), errno);
  }
}

poller_t::timer_t poller_t::add_timer_event(std::time_t in_secs, std::function<void(void)> f){
  auto now = std::time(nullptr);
  auto timer_id = max_timer_id++;
  timer_events.push_back(std::make_tuple(now + in_secs, timer_id, f));
  std::sort(std::begin(timer_events), std::end(timer_events), [](const auto &a, const auto &b){
    return std::get<0>(a) > std::get<0>(b);
  });
  // DEBUG("Added timer {}. {} s ({} pending)", timer_id, in_secs, timer_events.size());
  return poller_t::timer_t(timer_id);
}

void poller_t::call_later(std::function<void(void)> later_f){
  later_events.push_back(std::move(later_f));
}


void poller_t::remove_fd(int fd){
  fd_events.erase(fd);
  if (is_open()){
    auto r = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    if (r == -1){
      throw exception("Can't remove fd {} from poller: {} ({})", fd, strerror(errno), errno);
    }
  }
}

void poller_t::remove_timer(timer_t &tid){
  timer_events.erase(std::remove_if(
    timer_events.begin(), timer_events.end(),
    [&tid](const auto &b){
      // DEBUG("Remove {}. {}? {}", tid.id, std::get<1>(b), std::get<1>(b) == tid.id);
      return (std::get<1>(b) == tid.id);
    }),
    timer_events.end()
  );
  // DEBUG("Remove timer {}. {} left", tid.id, timer_events.size());
  // Invalidate
  tid.id = 0;
}

void poller_t::wait(){
  const auto MAX_EVENTS = 10;
  struct epoll_event events[MAX_EVENTS];
  auto wait_ms = -1;

  if (!timer_events.empty()){
    auto now = std::time(nullptr);
    wait_ms = (std::get<0>(timer_events[0]) - now) * 1000;
    // DEBUG("Timer event in {}ms", wait_ms);
    wait_ms = std::max(wait_ms, 0); // min wait 0ms.
  }

  auto nfds = 0;
  if (wait_ms != 0) // Maybe no wait. Some timer event pending.
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, wait_ms);

  for(auto n=0; n<nfds; n++){
    auto fd = events[n].data.fd;
    try{
      this->fd_events[fd](fd);
    } catch (const std::exception &e){
      ERROR("Catched exception at poller: {}", e.what());
    }
  }
  if (nfds == 0 && !timer_events.empty()){ // This was a timeout
    // Will ensure remove via RTTI
    {
      auto firstI = timer_events.begin();
      timer_t id = std::get<1>(*firstI);
      std::get<2>(*firstI)();
    }
    // if (!timer_events.empty()){
    //   auto now = std::time(nullptr);
    //   // DEBUG("Next timer event {} in {} s. {} left.", std::get<1>(timer_events[0]), std::get<0>(timer_events[0]) - now, timer_events.size());
    // } else {
    //   DEBUG("No timer events.");
    // }
  }

  while(!later_events.empty()){
    std::vector<std::function<void(void)>> call_now;
    // Clean the later, get the now.
    std::swap(call_now, later_events);
    for (auto &f: call_now){
        f();
    }
  }
}

poller_t::timer_t::timer_t() : id(0) {
}
poller_t::timer_t::timer_t(int id_) : id(id_) {
}
poller_t::timer_t::timer_t(poller_t::timer_t &&other) {
  id = other.id;
  other.id = 0;
}
poller_t::timer_t::~timer_t() {
  if (id !=0 ){
    poller.remove_timer(*this);
  }
}
poller_t::timer_t &poller_t::timer_t::operator=(poller_t::timer_t &&other) {
  if (id !=0 ){
    poller.remove_timer(*this);
  }
  id = other.id;
  other.id = 0;

  return *this;
}

void poller_t::timer_t::disable(){
  poller.remove_timer(*this);
  id = 0;
}
