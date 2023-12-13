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

#include "rtpmidid/poller.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rtpmidid/signal.hpp>

struct AvahiClient;
typedef struct AvahiClient AvahiClient;
typedef struct AvahiPoll AvahiPoll;
typedef struct AvahiEntryGroup AvahiEntryGroup;
typedef struct AvahiServiceBrowser AvahiServiceBrowser;

namespace rtpmidid {
struct announcement_t {
  std::string name;
  int port;
};

class mdns_rtpmidi_t {
public:
  std::unique_ptr<AvahiPoll> poller_adapter;
  AvahiClient *client;
  AvahiEntryGroup *group;
  AvahiServiceBrowser *service_browser;
  std::vector<announcement_t> announcements;
  // name, address, port
  signal_t<const std::string &, const std::string &, const std::string &>
      discover_event;
  signal_t<const std::string &, const std::string &, const std::string &>
      remove_event;
  poller_t::listener_t watch_in_poller;
  poller_t::listener_t watch_out_poller;

  mdns_rtpmidi_t();
  ~mdns_rtpmidi_t();

  mdns_rtpmidi_t(const mdns_rtpmidi_t &) = delete;
  mdns_rtpmidi_t &operator=(const mdns_rtpmidi_t &) = delete;

  void setup_mdns_browser();
  void announce_all();
  void announce_rtpmidi(const std::string &name, const int32_t port);
  void unannounce_rtpmidi(const std::string &name, const int32_t port);
};
} // namespace rtpmidid
