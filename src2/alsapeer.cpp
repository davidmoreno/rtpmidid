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

#include "alsapeer.hpp"
#include "aseq.hpp"
#include "json.hpp"
#include "midipeer.hpp"

using namespace rtpmididns;

alsapeer_t::alsapeer_t(const std::string &name,
                       std::shared_ptr<rtpmidid::aseq> seq_)
    : seq(seq_), name_(name) {
  port = seq->create_port(name);
  INFO("Created alsapeer {}, port {}", name, port);
}

alsapeer_t::~alsapeer_t() { seq->remove_port(port); }

void alsapeer_t::send_midi(midipeer_id_t from, const mididata_t &) {}

json_t alsapeer_t::status() { return json_t{{"type", "alsapeer_t"}}; }