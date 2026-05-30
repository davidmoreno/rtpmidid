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
#include <string>
#include <vector>

namespace rtpmididns {

struct settings_t {
  std::string alsa_name = "rtpmidid";
  bool alsa_network = true;
  std::string control_filename = "/var/run/rtpmidid/control.sock";
  rtpmidid::logger_level_t log_level = rtpmidid::logger_level_t::INFO;

  struct rtpmidi_discover_t {
    bool enabled = true;
    std::regex name_positive_regex = std::regex(".*");
    std::regex name_negative_regex = std::regex("^$");
  };

  rtpmidi_discover_t rtpmidi_discover;

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

  /** INI `[peer]` startup peers — full device identity strings. */
  struct ini_peer_t {
    std::string identity;
  };
  /** INI `[connect]` startup router edges (identity sides). */
  struct ini_connect_t {
    std::string from;
    std::string to;
    std::optional<std::string> direction; // a2b | b2a | both
  };
  std::vector<ini_peer_t> ini_peers;
  std::vector<ini_connect_t> ini_connects;

  /** Web UI: HTTP static + WebSocket JSON-RPC (see [web] in ini). */
  struct web_t {
    bool enabled = true;
    std::string listen = "127.0.0.1";
    int port = 8089;
    /** Directory with Parcel output (index.html). Empty after load → `frontend/dist`. */
    std::string root;
    std::string username;
    std::string password;
  };
  web_t web;

  /** Persisted router connections (see [database] in ini). Empty path disables. */
  struct database_t {
    std::string path;
  };
  database_t database;
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

BASIC_FORMATTER(rtpmididns::settings_t::alsa_hw_auto_export_t,
                "alsa_hw_auto_export_t[{}, {}, {}]", v.name_positive,
                v.name_negative, v.type);
BASIC_FORMATTER(::std::regex, "regex[{}]", "??");
BASIC_FORMATTER(rtpmididns::settings_t::rtpmidi_discover_t,
                "rtpmidi_discover_t[{}, {}, {}]", v.enabled,
                v.name_positive_regex, v.name_negative_regex);

BASIC_FORMATTER(rtpmididns::settings_t::web_t, "web_t[enabled={}, {}:{} root={}]",
                v.enabled, v.listen, v.port, v.root);
BASIC_FORMATTER(rtpmididns::settings_t,
                "settings_t[{}, {}, {}, {}, {}, {}, {}, ini_peers={}, "
                "ini_connects={}]",
                v.alsa_name, v.alsa_network, v.control_filename, v.log_level,
                v.rtpmidi_discover, v.alsa_hw_auto_export, v.web,
                v.ini_peers.size(), v.ini_connects.size());
