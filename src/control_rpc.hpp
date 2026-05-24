/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace rtpmidid {
class mdns_rtpmidi_t;
}

namespace rtpmididns {
class midirouter_t;
class aseq_t;
class connection_db_manager_t;

struct control_rpc_context_t {
  std::shared_ptr<midirouter_t> router;
  std::shared_ptr<aseq_t> aseq;
  std::shared_ptr<rtpmidid::mdns_rtpmidi_t> mdns;
  std::shared_ptr<connection_db_manager_t> connection_db;
};

/** One line in, one line out (includes trailing newline). */
std::string control_rpc_dispatch_line(control_rpc_context_t &ctx, std::string_view line);

} // namespace rtpmididns
