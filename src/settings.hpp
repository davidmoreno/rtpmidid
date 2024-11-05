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

#include "rtpmidid/logger.hpp"
#include <optional>
#include <regex>

namespace rtpmididns {

struct settings_t {
  std::string alsa_name = "rtpmidid";
  bool alsa_network = true;
  std::string control_filename = "/var/run/rtpmidid/control.sock";

  // Datas a read from the ini file
  struct rtpmidi_announce_t {
    std::string name;
    std::string port;
  };

  struct rtpmidi_discover_t {
    bool enabled = true;
    std::regex name_positive_regex = std::regex(".*");
    std::regex name_negative_regex = std::regex("^$");
  };

  struct alsa_announce_t {
    std::string name;
  };

  struct connect_to_t {
    std::string hostname;
    std::string port;
    std::string name;
    std::string local_udp_port;
  };

  std::vector<rtpmidi_announce_t> rtpmidi_announces;
  rtpmidi_discover_t rtpmidi_discover;
  std::vector<alsa_announce_t> alsa_announces;
  std::vector<connect_to_t> connect_to;

  enum alsa_hw_auto_export_type_e {
    NONE = 0,
    ALL = 7,
    HARDWARE = 1,
    SOFTWARE = 2,
    SYSTEM = 4,
  };

  struct alsa_hw_auto_export_t {
    std::string name_positive;
    std::optional<std::regex> name_positive_regex;
    std::string name_negative;
    std::optional<std::regex> name_negative_regex;
    alsa_hw_auto_export_type_e type = alsa_hw_auto_export_type_e::NONE;
  };

  alsa_hw_auto_export_t alsa_hw_auto_export;

  struct rawmidi_t {
    std::string device;
    std::string name;
    std::string local_udp_port;
    std::string remote_udp_port;
    std::string hostname;
  };

  std::vector<rawmidi_t> rawmidi;
};

extern settings_t settings; // NOLINT
} // namespace rtpmididns

ENUM_FORMATTER_BEGIN(rtpmididns::settings_t::alsa_hw_auto_export_type_e);
ENUM_FORMATTER_ELEMENT(rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE,
                       "NONE");
ENUM_FORMATTER_ELEMENT(rtpmididns::settings_t::alsa_hw_auto_export_type_e::ALL,
                       "ALL");
ENUM_FORMATTER_ELEMENT(
    rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE, "HARDWARE");
ENUM_FORMATTER_ELEMENT(
    rtpmididns::settings_t::alsa_hw_auto_export_type_e::SOFTWARE, "SOFTWARE");
ENUM_FORMATTER_ELEMENT(
    rtpmididns::settings_t::alsa_hw_auto_export_type_e::SYSTEM, "SYSTEM");
ENUM_FORMATTER_END();

BASIC_FORMATTER(rtpmididns::settings_t::rawmidi_t, "rawmidi_t[{}, {}]",
                v.device, v.name);
BASIC_FORMATTER(rtpmididns::settings_t::connect_to_t,
                "connect_to_t[{}, {}, {}]", v.hostname, v.port, v.name);
BASIC_FORMATTER(rtpmididns::settings_t::alsa_hw_auto_export_t,
                "alsa_hw_auto_export_t[{}, {}, {}]", v.name_positive,
                v.name_negative, v.type);
BASIC_FORMATTER(rtpmididns::settings_t::rtpmidi_announce_t,
                "rtpmidi_announce_t[{}, {}]", v.name, v.port);
BASIC_FORMATTER(rtpmididns::settings_t::alsa_announce_t, "alsa_announce_t[{}]",
                v.name);
BASIC_FORMATTER(::std::regex, "regex[{}]", "??");
BASIC_FORMATTER(rtpmididns::settings_t::rtpmidi_discover_t,
                "rtpmidi_discover_t[{}, {}, {}]", v.enabled,
                v.name_positive_regex, v.name_negative_regex);

VECTOR_FORMATTER(rtpmididns::settings_t::rtpmidi_announce_t);
VECTOR_FORMATTER(rtpmididns::settings_t::alsa_announce_t);
VECTOR_FORMATTER(rtpmididns::settings_t::connect_to_t);

BASIC_FORMATTER(rtpmididns::settings_t,
                "settings_t[{}, {}, {}, {}, {}, {}, {}, {}]", v.alsa_name,
                v.alsa_network, v.control_filename, v.rtpmidi_announces,
                v.rtpmidi_discover, v.alsa_announces, v.connect_to,
                v.alsa_hw_auto_export);
