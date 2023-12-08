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
#include <chrono>
#include <functional>
#include <math.h>

namespace rtpmidid {
/**
 * @short stores staticsits invformation about the connection
 *
 * Can feed the class with new latency measurements,
 * and can produce a mean value and a standard deviation.
 *
 * Keeps recent measurements up to 10, and when each measurement is added.
 * The average and mean is only the last 2 minutes of measurements.
 */
class stats_t {
public:
  struct stat_t {
    std::chrono::nanoseconds latency;
    std::chrono::system_clock::time_point timestamp;
  };

private:
  // Use it as a circular buffer,only overwrite when full
  // but as we only keep the last 2 minutes, it will be fine
  std::vector<stat_t> stats;
  int index = 0;
  std::chrono::seconds item_time;

public:
  stats_t(int size = 20,
          std::chrono::seconds item_time_ = std::chrono::seconds(120))
      : item_time(item_time_) {
    stats.resize(size);
  }

  void add_stat(std::chrono::nanoseconds latency) {
    stats[index].latency = latency;
    stats[index].timestamp = std::chrono::system_clock::now();
    DEBUG("Added stats {}us", latency.count());
    index = (index + 1) % stats.size();
  }

  void loop_stats(std::function<void(stat_t const &)> const &f) {
    auto now = std::chrono::system_clock::now();
    for (auto &stat : stats) {
      if (now - stat.timestamp > item_time) {
        break;
      }
      f(stat);
    }
  }

  struct average_and_stddev_t {
    std::chrono::nanoseconds average;
    std::chrono::nanoseconds stddev;
  };

  average_and_stddev_t average_and_stddev() {
    double sum = 0;
    int count = 0;
    loop_stats([&](stat_t const &stat) {
      sum += stat.latency.count();
      count++;
    });
    if (count == 0) {
      return average_and_stddev_t{
          std::chrono::nanoseconds(0),
          std::chrono::nanoseconds(0),
      };
    }
    auto average = sum / count;
    sum = 0;
    loop_stats([&](stat_t const &stat) {
      auto mean = stat.latency.count();
      auto delta = mean - average;
      sum += delta * delta;
    });
    auto stddev = sqrt(sum / count);

    return average_and_stddev_t{
        std::chrono::nanoseconds((int)average),
        std::chrono::nanoseconds((int)stddev),
    };
  }
};
} // namespace rtpmidid