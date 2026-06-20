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

#include "rtpmidid/threading_types.hpp"
#include <pthread.h>
#include <random>
#include <sched.h>

namespace rtpmidid {

void drop_realtime_scheduling() {
  struct sched_param param {};
  param.sched_priority = 0;
  if (pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) != 0)
    return; // best-effort; the thread will still work
}

uint32_t rand_u32(void) {
  static std::random_device random_device;
  static std::mt19937 generator_mt19937(random_device());
  static std::uniform_int_distribution<uint32_t> distrib32;

  return distrib32(generator_mt19937);
}
} // namespace rtpmidid