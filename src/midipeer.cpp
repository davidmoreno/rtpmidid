/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
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

#include "midipeer.hpp"
#include "midirouter.hpp"
#include <rtpmidid/logger.hpp>

#include "json.hpp"

namespace rtpmididns {

midipeer_t::~midipeer_t() {
  if (router) {
    router->remove_peer(peer_id);
  }
}
json_t midipeer_t::command(const std::string &cmd, const json_t &data) {
  ERROR("Unknown command: {}", cmd);
  if (cmd == "help") {
    return {json_t::object({})};
  }
  if (cmd == "status") {
    return status();
  }
  return json_t({
      {"error", "Command not implemented"},
  });
}
} // namespace rtpmididns
