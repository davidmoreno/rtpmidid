/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

#include <cstddef>

/**
 * @short Counts C++ heap allocations for the tests.
 *
 * The library must not allocate once a session is up: the receive/send MIDI
 * paths, the recovery journal and the guard/feedback packets are real time.
 * malloc_counter.cpp replaces global operator new/delete (which interposes over
 * librtpmidid, as the tests link it dynamically) so a test can assert that a
 * hot path performs exactly this many allocations: none.
 *
 * Counting is off until start(), so setup (peers, connect, packet building) is
 * not counted, and off again after stop() so the assertion machinery itself is
 * not either.
 */
namespace malloc_counter {

/// Reset the counter and start counting allocations.
void start();

/// Stop counting. Allocations still happen, they are just not counted.
void stop();

/// Allocations since the last start().
size_t count();

} // namespace malloc_counter
