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

#include "./mdns_rtpmidi.hpp"
#include "./logger.hpp"
#include "./poller.hpp"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>

using namespace rtpmidid;

struct AvahiTimeout{
  rtpmidid::poller_t::timer_t timer_id;
  void *userdata;
  AvahiTimeoutCallback callback;
};

struct AvahiWatch {
  int fd;
  void *userdata;
  AvahiWatchCallback callback;
  AvahiWatchEvent event;
};

struct AvahiEntryGroup {
  char *name;
};

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
  mdns_rtpmidi *mr = (mdns_rtpmidi*) userdata;
  mr->group = g;

  /* Called whenever the entry group state changes */
  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED :
      /* The entry group has been established successfully */
      INFO("Service '{}' successfully established", g->name);
      break;
    case AVAHI_ENTRY_GROUP_COLLISION : {
      char *n;
      /* A service name collision with a remote service
       * happened. Let's pick a new name */
      n = avahi_alternative_service_name(g->name);
      avahi_free(g->name);
      g->name = n;
      ERROR("Service name collision, renaming service to '{}", g->name);
      /* And recreate the services */
      // create_services(avahi_entry_group_get_client(g));
      break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE :
      ERROR("Entry group failure: {}", avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
      /* Some kind of failure happened while we were registering our services */
      // avahi_simple_poll_quit(simple_poll);
      break;
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
        ;
  }
}

/// Create a new watch for the specified file descriptor and for the specified events. More...
AvahiWatch *poller_adapter_watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata){
  DEBUG("watch_new {} {}", fd, event);
  AvahiWatch *wd = new AvahiWatch;
  wd->fd = fd;
  wd->userdata = userdata;
  wd->callback = callback;

  wd->event = event;
  if (event == AVAHI_WATCH_IN) {
    poller.add_fd_in(fd, [wd](int _){
      wd->callback(wd, wd->fd, AVAHI_WATCH_IN, wd->userdata);
    });
  } else if (event == AVAHI_WATCH_OUT) {
    poller.add_fd_in(fd, [wd](int _){
      wd->callback(wd, wd->fd, AVAHI_WATCH_OUT, wd->userdata);
    });
  } else {
    DEBUG("Other event: {}", event);
  }

  return wd;
}

/// Update the events to wait for. More...
void poller_adapter_watch_update(AvahiWatch *wd, AvahiWatchEvent event) {
  poller.remove_fd(wd->fd);

  wd->event = event;
  if (event == AVAHI_WATCH_IN) {
    poller.add_fd_in(wd->fd, [wd](int _){
      wd->callback(wd, wd->fd, AVAHI_WATCH_IN, wd->userdata);
    });
  } else if (event == AVAHI_WATCH_OUT) {
    poller.add_fd_in(wd->fd, [wd](int _){
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
  poller.remove_fd(w->fd);
  delete w;
}

/// Set a wakeup time for the polling loop. More...
AvahiTimeout *poller_adapter_timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata) {
  AvahiTimeout *ret = new AvahiTimeout();
  ret->userdata = userdata;
  ret->callback = callback;
  ret->timer_id = 0;
  if (tv){
    ret->timer_id = poller.add_timer_event(tv->tv_sec, [ret]{
      ret->callback(ret, ret->userdata);
    });
  }
  return ret;
}

/// Update the absolute expiration time for a timeout, If tv is NULL, the timeout is disabled. More...
void poller_adapter_timeout_update(AvahiTimeout *to, const struct timeval *tv) {
  poller.remove_timer(to->timer_id);
  to->timer_id = 0;
  if (tv){
    to->timer_id = poller.add_timer_event(tv->tv_sec, [to]{
      to->callback(to, to->userdata);
    });
  }
}

/// Free a timeout. More...
void poller_adapter_timeout_free(AvahiTimeout *to) {
  poller.remove_timer(to->timer_id);
  delete to;
}

static void client_callback(AvahiClient *c, AvahiClientState state, void * userdata) {
    assert(c);
    DEBUG("Callback called {}", state);

    mdns_rtpmidi *mr = (mdns_rtpmidi*) userdata;
    mr->client = c;

    /* Called whenever the client or server state changes */
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            /* The server has startup successfully and registered its host
             * name on the network, so it's time to create our services */
            // create_services(c);
            mr->handle_connected();
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
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

mdns_rtpmidi::mdns_rtpmidi() {
  group = nullptr;
  poller_adapter = std::make_unique<AvahiPoll>();
  poller_adapter->watch_new = poller_adapter_watch_new;
  poller_adapter->watch_update = poller_adapter_watch_update;
  poller_adapter->watch_get_events = poller_adapter_watch_get_events;
  poller_adapter->watch_free = poller_adapter_watch_free;
  poller_adapter->timeout_new = poller_adapter_timeout_new;
  poller_adapter->timeout_update = poller_adapter_timeout_update;
  poller_adapter->timeout_free = poller_adapter_timeout_free;

  int error;
  client = avahi_client_new(poller_adapter.get(), (AvahiClientFlags)0, client_callback, this, &error);
  if (!client){
    ERROR("Error creating avahi client: {} {}", error, avahi_strerror(error));
  }
}

mdns_rtpmidi::~mdns_rtpmidi() {
  avahi_client_free(client);
}

void mdns_rtpmidi::handle_connected() {
  if (!group) {
    if (!(group = avahi_entry_group_new(client, entry_group_callback, this))) {
        ERROR("avahi_entry_group_new() failed: {}", avahi_strerror(avahi_client_errno(client)));
        goto fail;
    }
  }
  if (avahi_entry_group_is_empty(group)) {
    int ret;
    for(auto &entry: announcements) {
      ret = avahi_entry_group_add_service(
        group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0,
        entry.name.c_str(), "_apple-midi._udp", NULL, NULL,
        entry.port, NULL
      );
      if (ret < 0) {
        if (ret == AVAHI_ERR_COLLISION)
          goto collision;
        ERROR("Failed to add _ipp._tcp service: {}", avahi_strerror(ret));
        goto fail;
      }
    }
    if (announcements.size() > 0) {
      if ((ret = avahi_entry_group_commit(group)) < 0) {
        ERROR("Failed to commit entry group: {}", avahi_strerror(ret));
        goto fail;
      }
    }
  }
  return;
collision:
  ERROR("Name collision.");
  ;
fail:
  ;
}

void mdns_rtpmidi::announce_rtpmidi(const std::string &name, const int32_t port){
  int ret;
  announcement_t entry = {
    name, port
  };

  ret = avahi_entry_group_add_service(
    group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0,
    entry.name.c_str(), "_apple-midi._udp", NULL, NULL,
    entry.port, NULL
  );

  announcements.push_back(std::move(entry));

  if ((ret = avahi_entry_group_commit(group)) < 0) {
      ERROR("Failed to commit entry group: {}", avahi_strerror(ret));
  }

}

void mdns_rtpmidi::unannounce_rtpmidi(const std::string &name, const int32_t port){

}
