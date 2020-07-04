/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2020 David Moreno Montero <dmoreno@coralbits.com>
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
#include <functional>
#include <map>

template <typename... Args> class signal_t {
public:
  int connect(std::function<void(Args...)> const &&f) {
    auto cid = max_id++;
    slots[cid] = std::move(f);
    return cid;
  }

  void disconnect(int id) { slots.erase(id); }

  void disconnect_all() { slots.clear(); }

  void operator()(Args... args) {
    for (auto const &f : slots) {
      f.second(std::forward<Args>(args)...);
    }
  }

  size_t count() { return slots.size(); }

private:
  uint32_t max_id = 0;
  std::map<uint32_t, std::function<void(Args...)>> slots;
};
