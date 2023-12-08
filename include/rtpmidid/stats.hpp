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
#include <vector>

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
  struct average_and_stddev_t {
    std::chrono::nanoseconds average;
    std::chrono::nanoseconds stddev;
  };

private:
  // Use it as a circular buffer,only overwrite when full
  // but as we only keep the last 2 minutes, it will be fine
  std::vector<stat_t> stats;
  int index = 0;
  std::chrono::seconds item_time;

public:
  stats_t(int size = 20,
          std::chrono::seconds item_time_ = std::chrono::seconds(120));

  void add_stat(std::chrono::nanoseconds latency);
  void loop_stats(std::function<void(stat_t const &)> const &f) const;
  average_and_stddev_t average_and_stddev() const;
};
} // namespace rtpmidid