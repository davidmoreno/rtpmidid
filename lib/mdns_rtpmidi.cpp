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

#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <chrono>
#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/poller.hpp>

#include <algorithm>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

const char *RTPMIDI_MDNS_SERVICE_NAME = "_apple-midi._udp";

struct AvahiTimeout {
  void *userdata = nullptr;
  AvahiTimeoutCallback callback = nullptr;
  rtpmidid::poller_t::timer_t timer_id;
};

struct AvahiWatch {
  int fd = -1;
  void *userdata = nullptr;
  AvahiWatchCallback callback = nullptr;
  AvahiWatchEvent event = AVAHI_WATCH_IN;
};

// FIXME! Hack needed as at poller_adapter_watch_new I'm getting the wrong
// userdata pointer :(
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rtpmidid::mdns_rtpmidi_t *current = nullptr;

template <> struct fmt::formatter<AvahiWatchEvent> : fmt::formatter<int> {
  auto format(AvahiWatchEvent ev, fmt::format_context &ctx) const {
    return fmt::formatter<int>::format((int)ev, ctx);
  }
};
template <> struct fmt::formatter<AvahiBrowserEvent> : fmt::formatter<int> {
  auto format(AvahiBrowserEvent ev, fmt::format_context &ctx) const {
    return fmt::formatter<int>::format((int)ev, ctx);
  }
};
template <> struct fmt::formatter<AvahiEntryGroupState> : fmt::formatter<int> {
  auto format(AvahiEntryGroupState state, fmt::format_context &ctx) {
    return fmt::formatter<int>::format((int)state, ctx);
  }
};
template <> struct fmt::formatter<AvahiClientState> : fmt::formatter<int> {
  auto format(AvahiClientState state, fmt::format_context &ctx) {
    return fmt::formatter<int>::format((int)state, ctx);
  }
};
template <> struct fmt::formatter<AvahiLookupResultFlags> : fmt::formatter<int> {
  auto format(AvahiLookupResultFlags flag, fmt::format_context &ctx) {
    return fmt::formatter<int>::format((int)flag, ctx);
  }
};

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                                 AVAHI_GCC_UNUSED void *userdata) {
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;
  // not set yet, we assume (test by running, that its the rtpmidi group)
  if (mr->group == nullptr) {
    mr->group = g;
  }
  if (g != mr->group) {
    DEBUG("Received a message from an unknown group {} != {}. new state: {}",
          (void *)g, (void *)mr->group, state);
    return;
  }

  mr->entry_group_callback(state);
}

/// Create a new watch for the specified file descriptor and for the specified
/// events. More...
AvahiWatch *poller_adapter_watch_new(const AvahiPoll *api, int fd,
                                     AvahiWatchEvent event,
                                     AvahiWatchCallback callback,
                                     void *userdata) {
  // DEBUG("watch_new {} {}", fd, event);
  // NOLINTNEXTLINE
  AvahiWatch *wd = new AvahiWatch;
  wd->fd = fd;
  wd->userdata = userdata;
  wd->callback = callback;
  rtpmidid::mdns_rtpmidi_t *mdns_rtpmidid = current;
  // static_cast<rtpmidid::mdns_rtpmidi_t *>(userdata);
  // DEBUG("mdns {}", (void *)userdata);
  // DEBUG("mdns {}", (void *)mdns_rtpmidid);

  wd->event = event;
  if (event == AVAHI_WATCH_IN) {
    mdns_rtpmidid->watch_in_poller =
        rtpmidid::poller.add_fd_in(fd, [wd](int _) {
          wd->callback(wd, wd->fd, AVAHI_WATCH_IN, wd->userdata);
        });
  } else if (event == AVAHI_WATCH_OUT) {
    mdns_rtpmidid->watch_out_poller =
        rtpmidid::poller.add_fd_in(fd, [wd](int _) {
          wd->callback(wd, wd->fd, AVAHI_WATCH_OUT, wd->userdata);
        });
  } else {
    DEBUG("Other event: {}", event);
  }

  return wd;
}

/// Update the events to wait for. More...
void poller_adapter_watch_update(AvahiWatch *wd, AvahiWatchEvent event) {
  if (!current) {
    WARNING("Got a watch_update without a current mdns_rtpmidi_t");
    return;
  }
  rtpmidid::mdns_rtpmidi_t *mdns_rtpmidid = current;
  // rtpmidid::mdns_rtpmidi_t *mdns_rtpmidid =
  //     static_cast<rtpmidid::mdns_rtpmidi_t *>(wd->userdata);
  //

  wd->event = event;
  if (event == AVAHI_WATCH_IN) {
    mdns_rtpmidid->watch_in_poller =
        rtpmidid::poller.add_fd_in(wd->fd, [wd](int _) {
          wd->callback(wd, wd->fd, AVAHI_WATCH_IN, wd->userdata);
        });
  } else if (event == AVAHI_WATCH_OUT) {
    mdns_rtpmidid->watch_out_poller =
        rtpmidid::poller.add_fd_in(wd->fd, [wd](int _) {
          wd->callback(wd, wd->fd, AVAHI_WATCH_OUT, wd->userdata);
        });
  } else {
    DEBUG("Other event: {}", event);
  }
}

/// Return the events that happened. More...
AvahiWatchEvent poller_adapter_watch_get_events(AvahiWatch *w) {
  return w->event;
}

/// Free a watch. More...
void poller_adapter_watch_free(AvahiWatch *w) {
  // rtpmidid::poller.remove_fd(w->fd);
  // WARNING("TODO! If its only at program end, no problem.");
  if (current) {
    current->watch_in_poller.stop();
    current->watch_out_poller.stop();
  }
  // NOLINTNEXTLINE
  delete w;
}

/// Set a wakeup time for the polling loop. More...
AvahiTimeout *poller_adapter_timeout_new(const AvahiPoll *api,
                                         const struct timeval *tv,
                                         AvahiTimeoutCallback callback,
                                         void *userdata) {
  // NOLINTNEXTLINE
  AvahiTimeout *to = new AvahiTimeout();
  to->userdata = userdata;
  to->callback = callback;
  to->timer_id = 0;
  if (tv) {
    auto chrono_tv =
        std::chrono::milliseconds(tv->tv_sec * 1000 + tv->tv_usec / 1000);
    // if (chrono_tv.count() <= 0) {
    //   to->callback(to, to->userdata);
    //   to->timer_id = 0;
    // } else {
    to->timer_id = rtpmidid::poller.add_timer_event(chrono_tv, [to] {
      DEBUG("Timeout for to {} {}", (void *)to, to->timer_id.id);
      to->callback(to, to->userdata);
    });
    // }
  }
  return to;
}

/// Update the absolute expiration time for a timeout, If tv is NULL, the
/// timeout is disabled. More...
void poller_adapter_timeout_update(AvahiTimeout *to, const struct timeval *tv) {
  rtpmidid::poller.remove_timer(to->timer_id);
  if (tv) {
    auto chrono_tv =
        std::chrono::milliseconds(tv->tv_sec * 1000 + tv->tv_usec / 1000);
    // if (chrono_tv.count() <= 0) {
    //   // DEBUG("Inmediate call, timeout <= 0");
    //   to->callback(to, to->userdata);
    //   to->timer_id = 0;
    // } else {
    to->timer_id = rtpmidid::poller.add_timer_event(
        chrono_tv, [to] { to->callback(to, to->userdata); });
    // }
  }
}

/// Free a timeout. More...
void poller_adapter_timeout_free(AvahiTimeout *to) {
  rtpmidid::poller.remove_timer(to->timer_id);
  // NOLINTNEXTLINE
  delete to;
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            void *userdata) {
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;
  if (mr->client == nullptr) {
    mr->client = c;
  }
  mr->client_callback((rtpmidid::avahi_client_state_e)state);
}

// Just to give it proper names.. please come'on avahi, use a struct if that
// many parameters
struct rtpmidid::resolve_callback_s {
  AvahiServiceResolver *resolver;
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  AvahiResolverEvent event;
  const char *name;
  const char *type;
  const char *domain;
  const char *host_name;
  const AvahiAddress *address;
  uint16_t port;
  AvahiStringList *txt;
  AvahiLookupResultFlags flags;
};

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface,
                             AVAHI_GCC_UNUSED AvahiProtocol protocol,
                             AvahiResolverEvent event, const char *name,
                             const char *type, const char *domain,
                             const char *host_name, const AvahiAddress *address,
                             uint16_t port, AvahiStringList *txt,
                             AvahiLookupResultFlags flags,
                             AVAHI_GCC_UNUSED void *userdata) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  rtpmidid::resolve_callback_s data = {r,       interface, protocol, event,
                                       name,    type,      domain,   host_name,
                                       address, port,      txt,      flags};
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;

  mr->resolve_callback(data);
  avahi_service_resolver_free(r);
}

struct rtpmidid::browse_callback_s {
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  AvahiBrowserEvent event;
  const char *name;
  const char *type;
  const char *domain;
  AvahiLookupResultFlags flags;
};

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                            AvahiProtocol protocol, AvahiBrowserEvent event,
                            const char *name, const char *type,
                            const char *domain,
                            AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                            void *userdata) {
  rtpmidid::browse_callback_s data = {interface, protocol, event, name,
                                      type,      domain,   flags};

  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;
  mr->browse_callback(data);
}

rtpmidid::mdns_rtpmidi_t::mdns_rtpmidi_t() {
  current = this;

  service_browser = nullptr;
  group = nullptr;

  connect_to_avahi();
}

void rtpmidid::mdns_rtpmidi_t::setup_service_browser() {
  if (service_browser)
    return;
  service_browser = avahi_service_browser_new(
      client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, RTPMIDI_MDNS_SERVICE_NAME,
      NULL, (AvahiLookupFlags)0, ::browse_callback, this);
  if (!service_browser) {
    ERROR("Failed to create service browser: {}",
          avahi_strerror(avahi_client_errno(client)));
    return;
  }
}

void rtpmidid::mdns_rtpmidi_t::setup_entry_group() {
  if (group)
    return;
  group = avahi_entry_group_new(client, ::entry_group_callback, this);
  DEBUG("Avahi entry group created {}.", (void *)group);
  if (!group) {
    ERROR("avahi_entry_group_new() failed: {}",
          avahi_strerror(avahi_client_errno(client)));
    ERROR("Name collision?");
    return;
  }
}

/// Asks the network mdns for entries.
void rtpmidid::mdns_rtpmidi_t::connect_to_avahi() {
  INFO("Connecting to Avahi");
  if (client != nullptr) {
    ERROR("Client already connected. Doing nothing!");
    return;
  }
  int error = -1;

  DEBUG("mdns {}", (void *)this);
  poller_adapter = std::make_unique<AvahiPoll>();
  poller_adapter->watch_new = poller_adapter_watch_new;
  poller_adapter->watch_update = poller_adapter_watch_update;
  poller_adapter->watch_get_events = poller_adapter_watch_get_events;
  poller_adapter->watch_free = poller_adapter_watch_free;
  poller_adapter->timeout_new = poller_adapter_timeout_new;
  poller_adapter->timeout_update = poller_adapter_timeout_update;
  poller_adapter->timeout_free = poller_adapter_timeout_free;

  client = avahi_client_new(poller_adapter.get(),
                            (AvahiClientFlags)AVAHI_CLIENT_NO_FAIL,
                            ::client_callback, this, &error);
  if (!client) {
    ERROR("Error creating avahi client: {} {}. Try again in 30s", error,
          avahi_strerror(error));
    // Try again in 30s
    reconnect_timer = rtpmidid::poller.add_timer_event(
        std::chrono::milliseconds(1000), [this]() { connect_to_avahi(); });
  }
}

void rtpmidid::mdns_rtpmidi_t::close_avahi() {
  if (group) {
    avahi_entry_group_free(group);
    group = nullptr;
  }
  if (client) {
    avahi_client_free(client);
    client = nullptr;
  }
  if (service_browser) {
    avahi_service_browser_free(service_browser);
    service_browser = nullptr;
  }
}

rtpmidid::mdns_rtpmidi_t::~mdns_rtpmidi_t() {
  close_avahi();
  current = nullptr;
}

void rtpmidid::mdns_rtpmidi_t::client_callback(avahi_client_state_e state_) {
  AvahiClientState state = (AvahiClientState)state_;
  DEBUG("New avahi client state: {}", state);

  /* Called whenever the client or server state changes */
  switch (state) {
  case AVAHI_CLIENT_S_RUNNING:
    /* The server has startup successfully and registered its host
     * name on the network, so it's time to create our services */
    INFO("Client running");
    setup_service_browser();
    setup_entry_group();
    break;
  case AVAHI_CLIENT_FAILURE: {
    auto avahi_errno = avahi_client_errno(client);
    ERROR("Client failure: error=\"{}\" errno={}", avahi_strerror(avahi_errno),
          avahi_errno);
    if (avahi_errno == AVAHI_ERR_DISCONNECTED) {
      WARNING("Disconnected, reconnecting...It may get blocked. ");
    }
    // avahi_simple_poll_quit(simple_poll);
  } break;
  case AVAHI_CLIENT_S_COLLISION:
    /* Let's drop our registered services. When the server is back
     * in AVAHI_SERVER_RUNNING state we will register them
     * again with the new host name. */
    DEBUG("AVAHI_CLIENT_S_COLLISION");
  case AVAHI_CLIENT_S_REGISTERING:
    /* The server records are now being established. This
     * might be caused by a host name change. We need to wait
     * for our own records to register until the host name is
     * properly esatblished. */
    // if (group) {
    //   avahi_entry_group_reset(group);
    //   avahi_entry_group_commit(group);
    // }
    DEBUG("AVAHI_CLIENT_S_REGISTERING");
    break;
  case AVAHI_CLIENT_CONNECTING:;
    DEBUG("AVAHI_CLIENT_CONNECTING");
    break;
  default:
    DEBUG("Unknown client state: {}", state);
  }
}

void rtpmidid::mdns_rtpmidi_t::resolve_callback(
    const resolve_callback_s &data) {
  /* Called whenever a service has been resolved successfully or timed out */
  switch (data.event) {
  case AVAHI_RESOLVER_FAILURE: {
    auto avahi_errno = avahi_client_errno(client);
    const char *errmsg = avahi_strerror(avahi_errno);
    ERROR("AVAHI_RESOLVER_FAILURE Failed to resolve service=\"{}\" of type={}"
          "in domain={} host_name={} port={} with error_n={} error=\"{}\" ",
          data.name, data.type, data.domain,
          data.host_name == nullptr ? "null" : data.host_name, data.port,
          avahi_errno, errmsg);

    // We remove if it existed before. A resolver failure may be a timeout.
    remove_event(data.name, std::to_string(data.host_name || ""),
                 std::to_string(data.port));
  } break;
  case AVAHI_RESOLVER_FOUND: {
    if (!!(data.flags & AVAHI_LOOKUP_RESULT_OUR_OWN)) {
      DEBUG("Received own announcement of service={}. Ignore.", data.name);
      return;
    }
    std::array<char, AVAHI_ADDRESS_STR_MAX> avahi_address_str{};
    avahi_address_snprint(avahi_address_str.data(), avahi_address_str.size(),
                          data.address);
    DEBUG("Discovered service=\"{}\" in host={}:{} ip={} flags={:04X}",
          data.name, data.host_name, data.port, avahi_address_str.data(),
          data.flags);

    // FIXME: address is not correct for interface (!), so is not unique, how to
    // make unique? or filter on interface?
    discovered_remote(rtpmidid::remote_announcement_t{
        data.name,
        avahi_address_str.data(),
        data.port,
    });
  }
  }
}

void rtpmidid::mdns_rtpmidi_t::browse_callback(const browse_callback_s &data) {
  switch (data.event) {
  case AVAHI_BROWSER_FAILURE:
    ERROR("AVAHI_BROWSER_FAILURE {}",
          avahi_strerror(avahi_client_errno(client)));
    return;
  case AVAHI_BROWSER_NEW:
    if (!(avahi_service_resolver_new(client, data.interface, data.protocol,
                                     data.name, data.type, data.domain,
                                     AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0,
                                     ::resolve_callback, (void *)this))) {
      ERROR("AVAHI_BROWSER_NEW Failed to resolve service '{}': {}", data.name,
            avahi_strerror(avahi_client_errno(client)));
    }
    break;
  case AVAHI_BROWSER_REMOVE:
    INFO("(Browser) REMOVE: service=\"{}\" of type=\"{}\" in domain={} "
         "flags={:08X} ",
         data.name, data.type, data.domain, data.flags);
    if (data.flags & AVAHI_LOOKUP_RESULT_OUR_OWN) {
      DEBUG("Received own announcement removal. Ignore.");
      return;
    }
    removed_remote(data.name);
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    INFO("(Browser) {}", data.event == AVAHI_BROWSER_CACHE_EXHAUSTED
                             ? "CACHE_EXHAUSTED"
                             : "ALL_FOR_NOW");
    break;
  default:
    WARNING("AVAHI unknown event: %s", data.event);
  }
}

void rtpmidid::mdns_rtpmidi_t::entry_group_callback(
    entry_group_state_e state_) {
  AvahiEntryGroupState state = (AvahiEntryGroupState)state_;

  DEBUG("Entry group state: {}", state);

  /* Called whenever the entry group state changes */
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    INFO("AVAHI_ENTRY_GROUP_ESTABLISHED Group successfully established.");
    // announce_all();
    break;
  case AVAHI_ENTRY_GROUP_COLLISION: {
    DEBUG("AVAHI_ENTRY_GROUP_COLLISION. Will try another name?");
    announce_all();
    break;
  }
  case AVAHI_ENTRY_GROUP_FAILURE:
    ERROR("Entry group failure: {}",
          avahi_strerror(avahi_client_errno(client)));
    /* Some kind of failure happened while we were registering our services */
    // avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    DEBUG("AVAHI_ENTRY_GROUP_UNCOMMITED.");
    // mr->announce_all();
    // avahi_entry_group_commit(g);
    break;
  case AVAHI_ENTRY_GROUP_REGISTERING:
    DEBUG("AVAHI_ENTRY_GROUP_REGISTERING");
    break;
    ;
  }
}

void rtpmidid::mdns_rtpmidi_t::announce_all() {
  if (!group) {
    WARNING("No group to announce to. Maybe not connected to avahi yet. Will "
            "be annoucned when connected.");
    return;
  }
  auto state = avahi_entry_group_get_state(group);
  switch (state) {
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    DEBUG("AVAHI_ENTRY_GROUP_UNCOMMITED.");
    break;
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    DEBUG("AVAHI_ENTRY_GROUP_ESTABLISHED.");
    break;
  case AVAHI_ENTRY_GROUP_REGISTERING:
    DEBUG("AVAHI_ENTRY_GROUP_REGISTERING.");
    break;
  case AVAHI_ENTRY_GROUP_COLLISION:
    DEBUG("AVAHI_ENTRY_GROUP_COLLISION.");
    announce_suffix++;
    break;
  default:
    WARNING("Group not established yet. Will be announced when established. "
            "State={}",
            state);
    return;
  }

  avahi_entry_group_reset(group);

  std::string announce_suffix_str = "";
  if (announce_suffix > 0) {
    announce_suffix_str = fmt::format(" #{}", announce_suffix);
  }

  int ret = -1;
  for (auto &entry : announcements) {
    auto name = entry.name + announce_suffix_str;
    DEBUG("Announce: name=\"{}\" port={}", name, entry.port);
    ret = avahi_entry_group_add_service(
        group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
        // (AvahiPublishFlags)AVAHI_PUBLISH_USE_MULTICAST,
        (AvahiPublishFlags)0, name.c_str(), RTPMIDI_MDNS_SERVICE_NAME, NULL,
        NULL, entry.port, NULL);
    if (ret < 0) {
      ERROR("Failed to add service={} name=\"{}\" group state={} msg=\"{}\"",
            RTPMIDI_MDNS_SERVICE_NAME, entry.name,
            avahi_entry_group_get_state(group), avahi_strerror(ret));
      announce_suffix++;
      return;
    }
  }
  if (announcements.size() < 0) {
    DEBUG("Nothing to commit");
    return;
  }
  ret = avahi_entry_group_commit(group);
  if (ret < 0) {
    ERROR("Failed to commit entry group: {}", avahi_strerror(ret));
    return;
  }
  INFO("Announced {} services", announcements.size());
}

void rtpmidid::mdns_rtpmidi_t::announce_rtpmidi(const std::string &name,
                                                const int32_t port) {
  DEBUG("Announce {}", name);
  announcements.push_back({name, port});

  announce_all();
}

void rtpmidid::mdns_rtpmidi_t::unannounce_rtpmidi(const std::string &name,
                                                  const int32_t port) {
  DEBUG("Unannounce {}", name);
  announcements.erase(std::remove_if(announcements.begin(), announcements.end(),
                                     [port](const announcement_t &t) {
                                       return port == t.port;
                                     }),
                      announcements.end());
  announce_all();
}

void rtpmidid::mdns_rtpmidi_t::discovered_remote(
    const remote_announcement_t &remote) {
  discover_event(remote.name, remote.address, std::to_string(remote.port));
  remote_announcements.push_back(remote);
}

void rtpmidid::mdns_rtpmidi_t::removed_remote(const std::string &name) {
  // We copy as remove modifies the original
  auto remote_annoucements_copy{this->remote_announcements};
  for (auto remote : remote_annoucements_copy) {
    if (remote.name == name) {
      remove_event(name, remote.address, std::to_string(remote.port));
      remove_announcement(name, remote.address, remote.port);
    }
  }
}

void rtpmidid::mdns_rtpmidi_t::remove_announcement(const std::string &name,
                                                   const std::string &address,
                                                   int port) {
  if (address == "") {
    unannounce_rtpmidi(name, port);
  } else {
    remote_announcements.erase(
        std::remove_if(
            remote_announcements.begin(), remote_announcements.end(),
            [name](const remote_announcement_t &t) { return name == t.name; }),
        remote_announcements.end());
    remove_event(name, address, std::to_string(port));
  }
}
