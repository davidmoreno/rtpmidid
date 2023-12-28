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

#pragma once

#include "json_fwd.hpp"
#include "rtpmidid/utils.hpp"
#include <cstdint>
#include <memory>

namespace rtpmididns {

using midipeer_id_t = uint32_t;

class mididata_t;
class midirouter_t;
/**
 * @short Any peer that can read and write midi
 *
 * Must be inherited by the real clients
 */
class midipeer_t : public std::enable_shared_from_this<midipeer_t> {
  NON_COPYABLE_NOR_MOVABLE(midipeer_t);
public:
  std::shared_ptr<midirouter_t> router;
  midipeer_id_t peer_id = 0;
  int packets_sent = 0;
  int packets_recv = 0;

  midipeer_t() = default;
  virtual ~midipeer_t();

  virtual json_t status() = 0;
  virtual void send_midi(midipeer_id_t from, const mididata_t &) = 0;
  virtual json_t command(const std::string &cmd, const json_t &data);
};
} // namespace rtpmididns
