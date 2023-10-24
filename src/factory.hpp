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

#include "rtpmidid/rtppeer.hpp"
#include <memory>
namespace rtpmidid {
class rtppeer_t;
class rtpclient_t;
} // namespace rtpmidid

namespace rtpmididns {
class aseq_t;
class midirouter_t;
class midipeer_t;

// Many factory creators, basically to allow testing of the different parts
std::shared_ptr<midipeer_t> make_alsalistener(const std::string &name,
                                              std::shared_ptr<aseq_t> aseq);
//
std::shared_ptr<midipeer_t> make_rtpmidilistener(const std::string &name,
                                                 const std::string &port,
                                                 std::shared_ptr<aseq_t> aseq);
//
std::shared_ptr<midipeer_t>
make_rtpmidiworker(std::shared_ptr<rtpmidid::rtppeer_t> peer);
//
std::shared_ptr<midipeer_t> make_alsaworker(const std::string &name,
                                            std::shared_ptr<aseq_t>);

//
std::shared_ptr<midipeer_t>
make_rtpmidiclientworker(std::shared_ptr<rtpmidid::rtpclient_t> peer);

//
std::shared_ptr<midipeer_t> make_rtpmidiserverworker(const std::string &name);
//
std::shared_ptr<midipeer_t> make_alsawaiter(const std::string &name,
                                            const std::string &hostname,
                                            const std::string &port,
                                            std::shared_ptr<aseq_t> aseq);

} // namespace rtpmididns