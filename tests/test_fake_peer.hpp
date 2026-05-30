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

#include "../src/midipeer.hpp"

#include <string>

namespace rtpmididns {

/** Configurable ALSA-seq peer for registry/connection_db tests. */
class fake_alsa_seq_peer_t : public midipeer_t {
public:
  fake_alsa_seq_peer_t(std::string name, std::string client = "Peak",
                       std::string port = "In");

  void send_midi(midipeer_id_t /*from*/, const mididata_t &) override {}
  const char *get_type() const override;
  router_peer_row_t status() const override;

private:
  std::string name_;
  std::string client_;
  std::string port_;
};

} // namespace rtpmididns
