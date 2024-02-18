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

#include <rtpmidid/logger.hpp>
#include <rtpmidid/mdns_rtpmidi.hpp>
#include <rtpmidid/poller.hpp>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

struct AvahiTimeout {
  rtpmidid::poller_t::timer_t timer_id;
  void *userdata = nullptr;
  AvahiTimeoutCallback callback = nullptr;
};

struct AvahiWatch {
  int fd = -1;
  void *userdata = nullptr;
  AvahiWatchCallback callback = nullptr;
  AvahiWatchEvent event = AVAHI_WATCH_IN;
};

struct AvahiEntryGroup {
  char *name;
};

// FIXME! Hack needed as at poller_adapter_watch_new I'm getting the wrong
// userdata pointer :(
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rtpmidid::mdns_rtpmidi_t *current = nullptr;

template <> struct fmt::formatter<AvahiWatchEvent> : fmt::formatter<int> {
  auto format(AvahiWatchEvent ev, fmt::format_context &ctx) {
    return fmt::formatter<int>::format((int)ev, ctx);
  }
};
template <> struct fmt::formatter<AvahiBrowserEvent> : fmt::formatter<int> {
  auto format(AvahiBrowserEvent ev, fmt::format_context &ctx) {
    return fmt::formatter<int>::format((int)ev, ctx);
  }
};

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                                 AVAHI_GCC_UNUSED void *userdata) {
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;
  mr->group = g;

  /* Called whenever the entry group state changes */
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    INFO("Service '{}' successfully established", g->name);
    break;
  case AVAHI_ENTRY_GROUP_COLLISION: {
    char *n = nullptr;
    /* A service name collision with a remote service
     * happened. Let's pick a new name */
    n = avahi_alternative_service_name(g->name);
    ERROR("Service name '{}' collision, renaming service to '{}", g->name, n);
    avahi_free(g->name);
    g->name = n;
    /* And recreate the services */
    // create_services(avahi_entry_group_get_client(g));
    break;
  }
  case AVAHI_ENTRY_GROUP_FAILURE:
    ERROR("Entry group failure: {}",
          avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    /* Some kind of failure happened while we were registering our services */
    // avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
  case AVAHI_ENTRY_GROUP_REGISTERING:;
  }
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
  // rtpmidid::poller.remove_fd(wd->fd);
  rtpmidid::mdns_rtpmidi_t *mdns_rtpmidid = current;
  // rtpmidid::mdns_rtpmidi_t *mdns_rtpmidid =
  //     static_cast<rtpmidid::mdns_rtpmidi_t *>(wd->userdata);

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
  AvahiTimeout *ret = new AvahiTimeout();
  ret->userdata = userdata;
  ret->callback = callback;
  ret->timer_id = 0;
  if (tv) {
    ret->timer_id = rtpmidid::poller.add_timer_event(
        std::chrono::seconds(tv->tv_sec),
        [ret] { ret->callback(ret, ret->userdata); });
  }
  return ret;
}

/// Update the absolute expiration time for a timeout, If tv is NULL, the
/// timeout is disabled. More...
void poller_adapter_timeout_update(AvahiTimeout *to, const struct timeval *tv) {
  rtpmidid::poller.remove_timer(to->timer_id);
  if (tv) {
    to->timer_id = rtpmidid::poller.add_timer_event(
        std::chrono::seconds(tv->tv_sec),
        [to] { to->callback(to, to->userdata); });
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
  mr->client = c;

  /* Called whenever the client or server state changes */
  switch (state) {
  case AVAHI_CLIENT_S_RUNNING:
    /* The server has startup successfully and registered its host
     * name on the network, so it's time to create our services */
    // create_services(c);
    mr->announce_all();
    break;
  case AVAHI_CLIENT_FAILURE:
    ERROR("Client failure: {}", avahi_strerror(avahi_client_errno(c)));
    // avahi_simple_poll_quit(simple_poll);
    break;
  case AVAHI_CLIENT_S_COLLISION:
    /* Let's drop our registered services. When the server is back
     * in AVAHI_SERVER_RUNNING state we will register them
     * again with the new host name. */
  case AVAHI_CLIENT_S_REGISTERING:
    /* The server records are now being established. This
     * might be caused by a host name change. We need to wait
     * for our own records to register until the host name is
     * properly esatblished. */
    // if (group)
    //     avahi_entry_group_reset(group);
    break;
  case AVAHI_CLIENT_CONNECTING:;
  }
}

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
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;

  assert(r);
  /* Called whenever a service has been resolved successfully or timed out */
  switch (event) {
  case AVAHI_RESOLVER_FAILURE:
    ERROR("(Resolver) Failed to resolve service '{}' of type '{}' in domain "
          "'{}': {}",
          name, type, domain,
          avahi_strerror(
              avahi_client_errno(avahi_service_resolver_get_client(r))));
    mr->remove_event(name, std::to_string(host_name || ""),
                     std::to_string(port));
    break;
  case AVAHI_RESOLVER_FOUND: {
    if (!!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN)) {
      // DEBUG("Received own announcement");
      return;
    }
    std::array<char, AVAHI_ADDRESS_STR_MAX> avahi_address_str{};
    avahi_address_snprint(avahi_address_str.data(), sizeof(avahi_address_str),
                          address);
    DEBUG("Discovered service '{:<32}' in host ({}:{} ({}))", name, host_name,
          port, avahi_address_str.data());
    // char *t = avahi_string_list_to_string(txt);
    // DEBUG("\t{}:{} ({})\n"
    //       "\tinterface={}\n"
    //       "\tTXT={}\n"
    //       "\tcookie is {}\n"
    //       "\tis_local: {}\n"
    //       "\tour_own: {}\n"
    //       "\twide_area: {}\n"
    //       "\tmulticast: {}\n"
    //       "\tcached: {}",
    //       host_name, port, a, interface, t,
    //       avahi_string_list_get_service_cookie(txt),
    //       !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
    //       !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
    //       !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
    //       !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
    //       !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
    // avahi_free(t);

    // FIXME: address is not correct for interface (!), so is not unique, how to
    // make unique? or filter on interface?
    mr->discover_event(name, avahi_address_str.data(), std::to_string(port));
  }
  }
  avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                            AvahiProtocol protocol, AvahiBrowserEvent event,
                            const char *name, const char *type,
                            const char *domain,
                            AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                            void *userdata) {
  // NOLINTNEXTLINE
  rtpmidid::mdns_rtpmidi_t *mr = (rtpmidid::mdns_rtpmidi_t *)userdata;

  switch (event) {
  case AVAHI_BROWSER_FAILURE:
    ERROR("(Browser) {}", avahi_strerror(avahi_client_errno(
                              avahi_service_browser_get_client(b))));
    return;
  case AVAHI_BROWSER_NEW:
    // INFO("(Browser) NEW: service '{}' of type '{}' in domain '{}' {}", name,
    //      type, domain, interface);
    /* We ignore the returned resolver object. In the callback
       function we free it. If the server is terminated before
       the callback function is called the server will free
       the resolver for us. */
    if (!(avahi_service_resolver_new(mr->client, interface, protocol, name,
                                     type, domain, AVAHI_PROTO_UNSPEC,
                                     (AvahiLookupFlags)0, resolve_callback,
                                     userdata)))
      ERROR("Failed to resolve service '{}': {}", name,
            avahi_strerror(avahi_client_errno(mr->client)));
    break;
  case AVAHI_BROWSER_REMOVE:
    INFO("(Browser) REMOVE: service '{}' of type '{}' in domain '{}'", name,
         type, domain);
    mr->remove_event(name, "*", "*");
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    INFO("(Browser) {}", event == AVAHI_BROWSER_CACHE_EXHAUSTED
                             ? "CACHE_EXHAUSTED"
                             : "ALL_FOR_NOW");
    break;
  default:
    WARNING("AVAHI unknown event: %s", event);
  }
}

rtpmidid::mdns_rtpmidi_t::mdns_rtpmidi_t() {
  current = this;

  poller_adapter = std::make_unique<AvahiPoll>();
  poller_adapter->watch_new = poller_adapter_watch_new;
  poller_adapter->watch_update = poller_adapter_watch_update;
  poller_adapter->watch_get_events = poller_adapter_watch_get_events;
  poller_adapter->watch_free = poller_adapter_watch_free;
  poller_adapter->timeout_new = poller_adapter_timeout_new;
  poller_adapter->timeout_update = poller_adapter_timeout_update;
  poller_adapter->timeout_free = poller_adapter_timeout_free;

  int error = -1;
  DEBUG("mdns {}", (void *)this);
  client = avahi_client_new(poller_adapter.get(), (AvahiClientFlags)0,
                            client_callback, this, &error);
  if (!client) {
    ERROR("Error creating avahi client: {} {}", error, avahi_strerror(error));
  }

  service_browser = nullptr;
  setup_mdns_browser();
}

/// Asks the network mdns for entries.
void rtpmidid::mdns_rtpmidi_t::setup_mdns_browser() {
  if (service_browser)
    avahi_service_browser_free(service_browser);
  service_browser = avahi_service_browser_new(
      client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_apple-midi._udp", NULL,
      (AvahiLookupFlags)0, browse_callback, this);
  if (!service_browser) {
    ERROR("Failed to create service browser: {}",
          avahi_strerror(avahi_client_errno(client)));
    return;
  }
}

rtpmidid::mdns_rtpmidi_t::~mdns_rtpmidi_t() {
  current = nullptr;
  avahi_client_free(client);
}

void rtpmidid::mdns_rtpmidi_t::announce_all() {
  if (!group) {
    group = avahi_entry_group_new(client, entry_group_callback, this);
    if (!group) {
      ERROR("avahi_entry_group_new() failed: {}",
            avahi_strerror(avahi_client_errno(client)));
      ERROR("Name collision.");
      return;
    }
  }
  if (!avahi_entry_group_is_empty(group)) {
    avahi_entry_group_reset(group);
  }
  int ret = -1;
  for (auto &entry : announcements) {
    DEBUG("Announce: {} {}", entry.name, entry.port);
    // NOLINTNEXTLINE
    ret = avahi_entry_group_add_service(
        group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
        (AvahiPublishFlags)AVAHI_PUBLISH_USE_MULTICAST, entry.name.c_str(),
        "_apple-midi._udp", NULL, NULL, entry.port, NULL);
    if (ret < 0) {
      if (ret == AVAHI_ERR_COLLISION) {
        ERROR("Name collision.");
        return;
      }
      ERROR("Failed to add _ipp._tcp service: {}", avahi_strerror(ret));
      return;
    }
  }
  if (announcements.size() > 0) {
    ret = avahi_entry_group_commit(group);
    if (ret < 0) {
      ERROR("Failed to commit entry group: {}", avahi_strerror(ret));
      return;
    }
  }
  return;
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
