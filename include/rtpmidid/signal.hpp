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

#pragma once

#include "logger.hpp"
#include <assert.h>
#include <cstdint>
#include <functional>
#include <map>

#define DEBUG0 DEBUG
// #define DEBUG0(...)

template <typename... Args> class connection_t;

template <typename... Args> class signal_t {
public:
  signal_t() { DEBUG0("{}::signal_t()", (void *)this); }
  ~signal_t() {
    DEBUG0("{}::~signal_t()", (void *)this);
    disconnect_all();
    DEBUG0("{}::~signal_t()~", (void *)this);
  }

  // Must keep the connection, when deleted will be disconnected
  [[nodiscard]] connection_t<Args...>
  connect(std::function<void(Args...)> const &&f) {
    auto cid = max_id++;
    slots[cid] = std::move(f);
    DEBUG0("{}::signal_t::connect(f) -> {}", (void *)this, cid);
    connections[cid] = nullptr;
    return connection_t(this, cid);
  }

  void disconnect(int id) {
    DEBUG0("{}::signal_t::disconnect({})", (void *)this, id);
    slots.erase(id);
    connections.erase(id);
  }

  void disconnect_all() {
    while (connections.begin() != connections.end()) {
      auto conn = connections.begin();
      DEBUG0("{}::signal_t::disconnect_all() {}", (void *)this,
             (void *)conn->second);
      conn->second->disconnect();
    }
    DEBUG0("{}::signal_t::disconnect_all(), has {}", (void *)this,
           slots.size());
    assert(slots.size() == 0);
    assert(connections.size() == 0);
  }

  /**
   * @short Calls the callback
   *
   * It has to be carefull to call all callbacks that are at call moment, if
   * they are still valid at call moment.
   *
   * This is as new callbacks can be added, or removed, and we should absolutely
   * not call a not valid callback anymore.
   */
  void operator()(Args... args) {
    DEBUG0("{}::signal_t::()", (void *)this);
    auto initial_list = slots;
    for (auto const &f : initial_list) {
      if (slots.find(f.first) == slots.end())
        continue; // this element was removed while looping, do not call
      f.second(std::forward<Args>(args)...);
    }
    DEBUG0("{}::signal_t::() END", (void *)this);
  }

  void replace_connection_ptr(int id, connection_t<Args...> *ptr) {
    DEBUG0("{}::replace_connection_ptr::({})", (void *)this, id);
    for (auto &f : connections) {
      DEBUG0("Got {}", f.first);
      if (f.first == id) {
        DEBUG0("{}::replace_connection_ptr::({} -> {})", (void *)this,
               (void *)f.second, (void *)ptr);
        f.second = ptr;
      }
    }
  }

  size_t count() { return slots.size(); }

private:
  int max_id = 1;
  std::map<int, std::function<void(Args...)>> slots;
  std::map<int, connection_t<Args...> *> connections;
};

template <typename... Args> class connection_t {
  signal_t<Args...> *signal;
  int id;

public:
  connection_t() : signal(nullptr), id(0) {
    DEBUG0("{}::connection_t()", (void *)this);
  }
  connection_t(signal_t<Args...> *signal_, int id_) : signal(signal_), id(id_) {
    DEBUG0("{}::connection_t({})", (void *)this, id_);
    signal->replace_connection_ptr(id, this);
  }
  connection_t(connection_t<Args...> &other) = delete;
  connection_t(connection_t<Args...> &&other) {
    DEBUG0("{}::connection_t({})", (void *)this, (void *)&other);
    signal = other.signal;
    id = other.id;
    if (signal)
      signal->replace_connection_ptr(id, this);

    other.signal = nullptr;
    other.id = 0;
  }

  ~connection_t() {
    DEBUG0("{}::~connection_t()", (void *)this);
    disconnect();
  }

  void operator=(connection_t<Args...> &&other) {
    DEBUG0("{}::=({})", (void *)this, (void *)&other);
    disconnect();
    signal = other.signal;
    id = other.id;
    signal->replace_connection_ptr(id, this);

    other.signal = nullptr;
    other.id = 0;
  }

  void disconnect() {
    DEBUG0("{}::disconnect()", (void *)this);
    if (id && signal)
      signal->disconnect(id);
    signal = nullptr;
    id = 0;
  }
#undef DEBUG0
};
