/**
 * Real Time Protocol Music Industry Digital Interface Daemon
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
  close(epollfd);
  epollfd = -1;
}

void poller_t::add_fd_inout(int fd, std::function<void(int)> f){
  events[fd] = f;
  struct epoll_event ev;

  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {}", fd, strerror(errno));
  }
}
void poller_t::add_fd_in(int fd, std::function<void(int)> f){
  events[fd] = f;
  struct epoll_event ev;

  ev.events = EPOLLIN;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {}", fd, strerror(errno));
  }
}
void poller_t::add_fd_out(int fd, std::function<void(int)> f){
  events[fd] = f;
  struct epoll_event ev;

  ev.events = EPOLLOUT;
  ev.data.fd= fd;
  auto r = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
  if (r == -1){
    throw exception("Can't add fd {} to poller: {}", fd, strerror(errno));
  }
}

void poller_t::remove_fd(int fd){
  events.erase(fd);
  auto r = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
  if (r == -1){
    throw exception("Can't remove fd {} from poller: {}", fd, strerror(errno));
  }
}

void poller_t::wait(int wait_ms){
  const auto MAX_EVENTS = 10;
  struct epoll_event events[MAX_EVENTS];

  auto nfds = epoll_wait(epollfd, events, MAX_EVENTS, wait_ms);
  for(auto n=0; n<nfds; n++){
    auto fd = events[n].data.fd;
    try{
      this->events[fd](fd);
    } catch (const std::exception &e){
      ERROR("Catched exception at poller: %e", e.what());
    }
  }
}
