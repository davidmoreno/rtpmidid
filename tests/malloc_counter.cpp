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

#include "malloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
// Constant initialized, so they are safe even if operator new runs during
// static initialization, before main().
std::atomic<bool> counting{false};
std::atomic<size_t> allocations{0};

void count_allocation() {
  if (counting.load(std::memory_order_relaxed))
    allocations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

// Global operator new/delete, replaced for the test binaries. The library is
// linked dynamically, so these interpose over it: see malloc_counter.hpp.

void *operator new(size_t size) {
  count_allocation();
  void *ptr = std::malloc(size ? size : 1);
  if (ptr == nullptr)
    throw std::bad_alloc();
  return ptr;
}

void *operator new[](size_t size) {
  count_allocation();
  void *ptr = std::malloc(size ? size : 1);
  if (ptr == nullptr)
    throw std::bad_alloc();
  return ptr;
}

void *operator new(size_t size, const std::nothrow_t &) noexcept {
  count_allocation();
  return std::malloc(size ? size : 1);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept {
  count_allocation();
  return std::malloc(size ? size : 1);
}

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { std::free(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept {
  std::free(ptr);
}
void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
  std::free(ptr);
}

namespace malloc_counter {

void start() {
  allocations.store(0, std::memory_order_relaxed);
  counting.store(true, std::memory_order_relaxed);
}

void stop() { counting.store(false, std::memory_order_relaxed); }

size_t count() { return allocations.load(std::memory_order_relaxed); }

} // namespace malloc_counter
