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

#include <arpa/inet.h>
#include <array>
#include <netdb.h>
#include <string_view>

namespace rtpmidid {
inline sockaddr *sockaddr_storage_to_sockaddr(sockaddr_storage *addr) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<sockaddr *>(addr);
}
inline int sockaddr_storage_get_port(sockaddr_storage *addr) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return ntohs(reinterpret_cast<sockaddr_in6 *>(addr)->sin6_port);
}
inline void sockaddr_storage_set_port(sockaddr_storage *addr, int port) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  reinterpret_cast<sockaddr_in6 *>(addr)->sin6_port = htons(port);
}
inline in6_addr *sockaddr_storage_get_addr_in6(sockaddr_storage *addr) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return &(reinterpret_cast<sockaddr_in6 *>(addr)->sin6_addr);
}

} // namespace rtpmidid
