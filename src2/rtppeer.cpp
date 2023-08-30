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

#include "rtppeer.hpp"
#include "json.hpp"
#include "mididata.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/poller.hpp"
#include "rtpmidid/rtppeer.hpp"
#include <memory>

namespace rtpmididns {
rtppeer_t::rtppeer_t(std::shared_ptr<rtpmidid::rtppeer_t> peer_) : peer(peer_) {

  midi_connection =
      peer->midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        router->send_midi(peer_id, mididata_t{data});
      });

  disconnect_connection = peer->disconnect_event.connect(
      [this](rtpmidid::rtppeer_t::disconnect_reason_e reason) {
        DEBUG("Peer disconnected: {}. Remove rtpmidi peer and alsa port too.",
              reason);
        rtpmidid::poller.call_later([this] {
          router->peer_connection_loop(peer_id, [this](auto other_peer) {
            router->remove_peer(other_peer->peer_id);
          });
          router->remove_peer(peer_id);
        });
      });
}

rtppeer_t::~rtppeer_t() {}

void rtppeer_t::send_midi(midipeer_id_t from, const mididata_t &data) {
  peer->send_midi(data);
};

json_t rtppeer_t::status() {
  return json_t{
      {"name", peer->remote_name}, //
      {"type", "rtppeer_t"},       //
      {"latency_ms", peer->latency / 10.0},
  };
};

} // namespace rtpmididns
