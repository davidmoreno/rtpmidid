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

#include "rtpmidiserverpeer.hpp"
#include "mididata.hpp"
#include "midipeer.hpp"
#include "midirouter.hpp"
#include "rtpmidid/iobytes.hpp"
#include "rtpmidid/mdns_rtpmidi.hpp"

namespace rtpmididns {
extern std::unique_ptr<::rtpmidid::mdns_rtpmidi> mdns;

rtpmidiserverpeer_t::rtpmidiserverpeer_t(const std::string &name_)
    : name(name_), server(name, "") {
  if (mdns)
    mdns->announce_rtpmidi(name, server.control_port);

  midi_connection =
      server.midi_event.connect([this](const rtpmidid::io_bytes_reader &data) {
        // DEBUG("Got data: {}", data.size());
        if (!router) {
          WARNING("Bad configured peer");
          return;
        }
        // rtpmididns::mididata_t mididata(data.start, data.pos());
        router->send_midi(this->peer_id, data);
      });
}
rtpmidiserverpeer_t::~rtpmidiserverpeer_t() {
  if (mdns)
    mdns->unannounce_rtpmidi(name, server.control_port);
}

void rtpmidiserverpeer_t::send_midi(midipeer_id_t from,
                                    const mididata_t &mididata) {
  server.send_midi_to_all_peers(mididata);
}
} // namespace rtpmididns
