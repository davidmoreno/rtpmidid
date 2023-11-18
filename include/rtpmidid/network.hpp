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

#include <arpa/inet.h>
#include <fmt/core.h>
#include <netdb.h>
#include <string_view>

template <>
struct fmt::formatter<sockaddr_storage> : formatter<std::string_view> {
  auto format(const sockaddr_storage &addr, format_context &ctx) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    host[0] = port[0] = '\0';
    getnameinfo((sockaddr *)&addr, sizeof(addr), host, NI_MAXHOST, port,
                NI_MAXSERV, NI_NUMERICSERV);
    std::string name = fmt::format("{}:{}", host, port);

    return formatter<std::string_view>::format(name, ctx);
  }
};
