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

#include "logger.hpp"
#include "utils.hpp"
#include <assert.h>
#include <cstdint>
#include <functional>
#include <map>

// #define DEBUG0 DEBUG
#define DEBUG0(...)

namespace rtpmidid {

template <typename... Args> class connection_t;

template <typename... Args> class signal_t {
  typedef std::map<int, std::function<void(Args...)>> VT;

public:
  signal_t() : slots(std::make_shared<VT>()) {
    DEBUG0("{}::signal_t()", (void *)this);
  }
  signal_t(signal_t<Args...> &&other) = delete;
  signal_t &operator=(signal_t<Args...> &&other) = delete;
  signal_t(const signal_t<Args...> &other) = delete;
  signal_t &operator=(const signal_t<Args...> &other) = delete;

  ~signal_t() {
    DEBUG0("{}::~signal_t()", (void *)this);
    disconnect_all();
    DEBUG0("{}::~signal_t()~", (void *)this);
  }

  // Must keep the connection, when deleted will be disconnected
  [[nodiscard]] connection_t<Args...>
  connect(std::function<void(Args...)> const &&f) {
    auto cid = max_id++;
    // Copy to next slots current slots, as if in use will still be valid, and
    // later will be replaced.
    slots = std::make_shared<VT>(*slots);
    slots->insert(std::make_pair(cid, std::move(f)));
    DEBUG0("{}::signal_t::connect(f) -> {}", (void *)this, cid);
    connections[cid] = nullptr;
    return connection_t(this, cid);
  }

  void disconnect(int id) {
    DEBUG0("{}::signal_t::disconnect({})", (void *)this, id);
    slots = std::make_shared<VT>(*slots);
    slots->erase(id);
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
           slots->size());
    assert(slots->size() == 0);
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
    auto slots = this->slots;
    DEBUG0("{}::signal_t::() {} slots", (void *)this, slots->size());
    for (auto const &f : *slots) {
      if (this->slots->find(f.first) == this->slots->end())
        continue; // this element was removed while looping, do not call
      DEBUG0("{}::signal_t::() calling {}", (void *)this, f.first);
      f.second(args...);
      DEBUG0("{}::signal_t::() called {}", (void *)this, f.first);
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

  size_t count() { return slots->size(); }

private:
  int max_id = 1;
  std::shared_ptr<VT> slots;

  std::map<int, connection_t<Args...> *> connections{};
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
  connection_t(connection_t<Args...> &&other) noexcept
      : signal(other.signal), id(other.id) {
    DEBUG0("{}::connection_t({})", (void *)this, (void *)&other);
    if (signal)
      signal->replace_connection_ptr(id, this);

    other.signal = nullptr;
    other.id = 0;
  }

  ~connection_t() {
    DEBUG0("{}::~connection_t()", (void *)this);
    disconnect();
  }
  connection_t(const connection_t<Args...> &) = delete;
  connection_t<Args...> &operator=(const connection_t<Args...> &) = delete;
  connection_t<Args...> &operator=(connection_t<Args...> &other) = delete;

  connection_t &operator=(connection_t<Args...> &&other) noexcept {
    DEBUG0("{}::=({})", (void *)this, (void *)&other);
    disconnect();
    signal = other.signal;
    id = other.id;
    signal->replace_connection_ptr(id, this);

    other.signal = nullptr;
    other.id = 0;

    return *this;
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
} // namespace rtpmidid