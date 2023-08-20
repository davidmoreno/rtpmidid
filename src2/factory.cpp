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

#include "factory.hpp"
#include "alsanetwork.hpp"
#include "alsapeer.hpp"
#include "midipeer.hpp"
#include "rtpmidiserverpeer.hpp"
#include <memory>

namespace rtpmididns {

std::shared_ptr<midipeer_t>
make_alsapeer(const std::string &name, std::shared_ptr<rtpmidid::aseq> aseq) {
  return std::make_shared<alsapeer_t>(name, aseq);
}

std::shared_ptr<midipeer_t> make_rtpmidiserver(const std::string &name) {
  return std::make_shared<rtpmidiserverpeer_t>(name);
}

} // namespace rtpmididns