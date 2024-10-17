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
#include "rtpmidid/utils.hpp"
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
struct resolve_callback_s;
struct browse_callback_s;
typedef int avahi_client_state_e;
typedef int entry_group_state_e;

struct announcement_t {
  std::string name;
  int port;
};
struct remote_announcement_t {
  std::string name;
  std::string address;
  int port;
};

class mdns_rtpmidi_t {
  NON_COPYABLE_NOR_MOVABLE(mdns_rtpmidi_t)
public:
  std::unique_ptr<AvahiPoll> poller_adapter;
  AvahiClient *client = nullptr;
  AvahiEntryGroup *group = nullptr;
  AvahiServiceBrowser *service_browser = nullptr;
  std::vector<announcement_t> announcements;
  std::vector<remote_announcement_t> remote_announcements;
  // name, address, port
  signal_t<const std::string &, const std::string &, const std::string &>
      discover_event;
  signal_t<const std::string &, const std::string &, const std::string &>
      remove_event;
  poller_t::listener_t watch_in_poller;
  poller_t::listener_t watch_out_poller;
  poller_t::timer_t reconnect_timer;
  int announce_suffix = 0;
  bool enable_service_browser = true;

  mdns_rtpmidi_t(bool enable_service_browser = false);
  ~mdns_rtpmidi_t();

  void connect_to_avahi();
  void close_avahi();

  void setup_service_browser();
  void setup_entry_group();

  void client_callback(avahi_client_state_e state);
  void resolve_callback(const resolve_callback_s &data);
  void browse_callback(const browse_callback_s &data);
  void entry_group_callback(entry_group_state_e state_);

  void announce_all();
  void announce_rtpmidi(const std::string &name, const int32_t port);
  void unannounce_rtpmidi(const std::string &name, const int32_t port);

  void discovered_remote(const remote_announcement_t &remote);
  void removed_remote(const std::string &name);

  // Can be a local or remote (address="" for local)
  void remove_announcement(const std::string &name, const std::string &address,
                           int port);
};
} // namespace rtpmidid
