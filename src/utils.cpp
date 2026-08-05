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
#include "peer_status.hpp"
#include "rtpmidid/rtppeer.hpp"

namespace rtpmididns {
peer_rtp_detail_t peer_status(rtpmidid::rtppeer_t &peer) {
  auto stats = peer.stats.average_and_stddev();
  peer_rtp_detail_t detail;
  detail.latency_ms = peer_latency_ms_t{
      peer.latency / 10.0,
      stats.average.count() / 1000.0,
      stats.stddev.count() / 1000.0,
  };
  detail.status = std::to_string(peer.status);
  detail.local = peer_local_t{
      peer.seq_nr,     peer.seq_nr_ack,           peer.local_name,
      peer.local_ssrc, peer.local_address.port(), peer.local_address.hostname(),
  };
  detail.remote = peer_remote_t{
      peer.remote_name,
      peer.remote_seq_nr,
      peer.remote_ssrc,
      peer.remote_address.port(),
      peer.remote_address.hostname(),
  };
  return detail;
}
} // namespace rtpmididns