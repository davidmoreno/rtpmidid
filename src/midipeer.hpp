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

#include "peer_status.hpp"
#include "rtpmidid/formatterhelper.hpp"
#include "rtpmidid/logger.hpp"
#include "rtpmidid/utils.hpp"
#include <limits>
#include <memory>
#include <string_view>

namespace rtpmididns {

using midipeer_id_t = uint32_t;
constexpr midipeer_id_t MIDIPEER_ID_INVALID =
    std::numeric_limits<uint32_t>::max();

class mididata_t;
class midirouter_t;

enum midipeer_event_e {
  CONNECTED_ROUTER = 1,
  DISCONNECTED_ROUTER,
  CONNECTED_PEER,
  DISCONNECTED_PEER,
};

} // namespace rtpmididns

ENUM_FORMATTER_BEGIN(rtpmididns::midipeer_event_e);
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::CONNECTED_ROUTER,
                       "CONNECTED_ROUTER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::DISCONNECTED_ROUTER,

                       "DISCONNECTED_ROUTER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::CONNECTED_PEER,
                       "CONNECTED_PEER");
ENUM_FORMATTER_ELEMENT(rtpmididns::midipeer_event_e::DISCONNECTED_PEER,
                       "DISCONNECTED_PEER");
ENUM_FORMATTER_DEFAULT();
ENUM_FORMATTER_END();

namespace rtpmididns {
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
   *  @brief Returns the status of the peer
   *
   * The typed per-peer status; the router assigns the common members
   * (id, send_to, stats, type) afterwards.
   *
   * @return peer_status_variant_t
   */
  virtual peer_status_variant_t status() = 0;
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
  virtual void event(midipeer_event_e event, midipeer_id_t from) {
    DEBUG("Peer event={} from={}", event, from);
  };
  /**
   * @brief Command as sent by the control interface
   *
   * Params arrive as raw JSON (string_view); implementations parse them
   * into their own typed structs and return the serialized result.
   *
   * @param cmd The command
   * @param params_json The params as JSON
   * @return The serialized JSON response
   */
  virtual std::string command(const std::string &cmd,
                              std::string_view params_json);
  /**
   * @brief Get the type of the peer
   */
  virtual const char *get_type() const = 0;
};
} // namespace rtpmididns

const char *format_as(rtpmididns::midipeer_event_e event);