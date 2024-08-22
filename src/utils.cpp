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

#include "utils.hpp"
#include "json.hpp"
#include "rtpmidid/rtppeer.hpp"

namespace rtpmididns {
json_t peer_status(rtpmidid::rtppeer_t &peer) {
  auto stats = peer.stats.average_and_stddev();
  return json_t{
      //
      {"latency_ms",
       {
           {"last", peer.latency / 10.0},
           {"average", stats.average.count() / 1000.0},
           {"stddev", stats.stddev.count() / 1000.0},
       }},
      {"status", std::to_string(peer.status)},
      {"local",
       {
           {"sequence_number", peer.seq_nr},            //
           {"sequence_number_ack", peer.seq_nr_ack},    //
           {"name", peer.local_name},                   //
           {"ssrc", peer.local_ssrc},                   //
           {"port", peer.local_address.port()},         //
           {"hostname", peer.local_address.hostname()}, //
       }},                                              //
      {
          "remote",
          {
              //
              {"name", peer.remote_name},                   //
              {"sequence_number", peer.remote_seq_nr},      //
              {"ssrc", peer.remote_ssrc},                   //
              {"port", peer.remote_address.port()},         //
              {"hostname", peer.remote_address.hostname()}, //
          } //
      }
      //
  };
}
} // namespace rtpmididns