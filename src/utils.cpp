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

namespace rtpmididns {
rtp_peer_status_t rtp_peer_status_from(const rtpmidid::rtppeer_t &peer) {
  rtp_peer_status_t out;
  auto stats = peer.stats.average_and_stddev();
  out.latency_ms.last = peer.latency / 10.0;
  out.latency_ms.average = stats.average.count() / 1000.0;
  out.latency_ms.stddev = stats.stddev.count() / 1000.0;
  out.status = std::to_string(static_cast<int>(peer.status));

  out.local.name = peer.local_name;
  out.local.hostname = peer.local_address.hostname();
  out.local.port = peer.local_address.port();
  out.local.ssrc = peer.local_ssrc;
  out.local.sequence_number = peer.seq_nr;
  out.local.sequence_number_ack = peer.seq_nr_ack;

  out.remote.name = peer.remote_name;
  out.remote.hostname = peer.remote_address.hostname();
  out.remote.port = peer.remote_address.port();
  out.remote.ssrc = peer.remote_ssrc;
  out.remote.sequence_number = peer.remote_seq_nr;
  out.remote.sequence_number_ack = std::nullopt;
  return out;
}
} // namespace rtpmididns
