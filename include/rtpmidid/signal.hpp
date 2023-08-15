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

namespace rtpmidid {
template <typename... Args> class connection_t;

template <typename... Args> class signal_t {
public:
  ~signal_t() { disconnect_all(); }

  // Must keep the connection, when deleted will be disconnected
  [[nodiscard]] connection_t<Args...>
  connect(std::function<void(Args...)> const &&f) {
    auto cid = max_id++;
    slots_[cid] = std::move(f);
    return connection_t(this, cid);
  }

  void disconnect(int id) {
    slots_.erase(id);
    connections.erase(id);
  }

  void disconnect_all() {
    for (auto &conn : connections) {
      conn.second->disconnect();
    }
    assert(slots_.size() == 0);
    assert(connections.size() == 0);
  }

  void operator()(Args... args) {
    for (auto const &f : slots_) {
      f.second(std::forward<Args>(args)...);
    }
  }

  size_t count() { return slots_.size(); }

private:
  uint32_t max_id = 1;
  std::map<uint32_t, std::function<void(Args...)>> slots_;
  std::map<uint32_t, connection_t<Args...> *> connections;
};

template <typename... Args> class connection_t {
  signal_t<Args...> *signal;
  int id;

public:
  connection_t() : signal(nullptr), id(0) {}
  connection_t(signal_t<Args...> *signal_, int id_)
      : signal(signal_), id(id_) {}
  connection_t(connection_t<Args...> &other) = delete;
  connection_t(connection_t<Args...> &&other) {
    disconnect();
    signal = other.signal;
    id = other.id;

    other.signal = nullptr;
    other.id = 0;
  }

  ~connection_t() { disconnect(); }

  void operator=(connection_t<Args...> &&other) {
    disconnect();
    signal = other.signal;
    id = other.id;

    other.signal = nullptr;
    other.id = 0;
  }

  void disconnect() {
    if (id && signal)
      signal->disconnect(id);
    signal = nullptr;
    id = 0;
  }
};
} // namespace rtpmidid