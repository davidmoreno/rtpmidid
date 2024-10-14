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
#include "rtpmidid/logger.hpp"
#include "rtpmidid/utils.hpp"
#include <limits>

namespace rtpmididns {

using midipeer_id_t = uint32_t;
constexpr midipeer_id_t MIDIPEER_ID_INVALID =
    std::numeric_limits<uint32_t>::max();

class mididata_t;
class midirouter_t;


enum midipeer_event_e{
    CONNECTED_ROUTER,
    DISCONNECTED_ROUTER,
    CONNECTED_PEER,
    DISCONNECTED_PEER,
};

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
  /// @brief statistics
  int packets_sent = 0;
  /// @brief statistics
  int packets_recv = 0;

  midipeer_t() = default;
  virtual ~midipeer_t();

  /**
   *  @brief Returns the status of the
   *
   * Basic data can be get with utils::peer_status
   *
   * @return  json_t
   */
  virtual json_t status() = 0;
  /**
   * @brief Send a midi message to the peer
   *
   * @param from The peer that sends the message
   * @param data The midi message
   */
  virtual void send_midi(midipeer_id_t from, const mididata_t &) = 0;
  /**
   * @brief Called when the peer is connected
   *
   * Normally do nothing, but might need to open a file and close
   * when all disconnect signas are received
   */
  virtual void event(midipeer_event_e event, midipeer_id_t from){
    DEBUG("Peer event={} from={}", event, from);
  };
  /**
   * @brief Command as sent by the control interface
   *
   * @param cmd The command
   * @param data The data
   * @return json_t The response
   */
  virtual json_t command(const std::string &cmd, const json_t &data);
  /**
   * @brief Get the type of the peer
   */
  virtual const char *get_type() const = 0;
};
} // namespace rtpmididns
