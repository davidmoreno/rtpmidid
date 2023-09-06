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
#include "alsalistener.hpp"
#include "alsaworker.hpp"
#include "midipeer.hpp"
#include "rtpmidilistener.hpp"
#include "rtpmidiserverworker.hpp"
#include "rtpmidiworker.hpp"
#include <memory>

namespace rtpmididns {

std::shared_ptr<midipeer_t> make_alsanetwork(const std::string &name,
                                             std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<alsalistener_t>(name, aseq);
}
std::shared_ptr<midipeer_t> make_rtpmididnetwork(const std::string &name,
                                                 const std::string &port,
                                                 std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<rtpmidilistener_t>(name, port, aseq);
}
std::shared_ptr<midipeer_t>
make_rtppeer(std::shared_ptr<rtpmidid::rtppeer_t> peer) {
  return std::make_shared<rtpmidiworker_t>(peer);
}

std::shared_ptr<midipeer_t> make_alsapeer(const std::string &name,
                                          std::shared_ptr<aseq_t> aseq) {
  return std::make_shared<alsaworker_t>(name, aseq);
}

std::shared_ptr<midipeer_t> make_rtpmidiserver(const std::string &name) {
  return std::make_shared<rtpmidiserverworker_t>(name);
}

} // namespace rtpmididns