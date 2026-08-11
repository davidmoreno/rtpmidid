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

#include "rtpmidid/jsondm.hpp"
#include "rtpmidid/logger.hpp"
#include <optional>
#include <regex>

namespace rtpmididns {

/// [INI-DM]
struct settings_t {
  std::string alsa_name = "rtpmidid";
  bool alsa_network = true;
  std::string control = "/var/run/rtpmidid/control.sock";
  rtpmidid::logger_level_t log_level = rtpmidid::logger_level_t::INFO;
  /// Real-time scheduling for data-plane actors (peers, router): promotes
  /// their threads to SCHED_FIFO at rt_priority (needs RLIMIT_RTPRIO or
  /// CAP_SYS_NICE); on failure falls back to nice-based elevation with a
  /// warning. Control/mdns run normal; the worker runs idle.
  bool rt_enable = true;
  int rt_priority = 10;

  // Datas a read from the ini file
  /// [INI-DM]
  struct rtpmidi_announce_t {
    std::string name;
    std::string port;
  };

  /// [INI-DM]
  struct rtpmidi_discover_t {
    bool enabled = true;
    std::regex name_positive_regex = std::regex(".*");
    std::regex name_negative_regex = std::regex("^$");
  };

  /// [INI-DM]
  struct alsa_announce_t {
    std::string name;
  };

  /// [INI-DM]
  struct connect_to_t {
    std::string hostname;
    std::string port;
    std::string name;
    std::string local_udp_port;
  };

  std::vector<rtpmidi_announce_t> rtpmidi_announce;
  rtpmidi_discover_t rtpmidi_discover;
  std::vector<alsa_announce_t> alsa_announce;
  std::vector<connect_to_t> connect_to;

  enum alsa_hw_auto_export_type_e {
    NONE = 0,
    ALL = 7,
    HARDWARE = 1,
    SOFTWARE = 2,
    SYSTEM = 4,
  };

  /// [INI-DM]
  struct alsa_hw_auto_export_t {
    std::optional<std::regex> name_positive_regex;
    std::optional<std::regex> name_negative_regex;
    alsa_hw_auto_export_type_e type = alsa_hw_auto_export_type_e::NONE;
  };

  alsa_hw_auto_export_t alsa_hw_auto_export;

  /// [INI-DM]
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
                "alsa_hw_auto_export_t[{}]", v.type);
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
                "settings_t[{}, {}, {}, {}, {}, {}, {}, {}, {}]", v.alsa_name,
                v.alsa_network, v.control, v.log_level, v.rtpmidi_announce,
                v.rtpmidi_discover, v.alsa_announce, v.connect_to,
                v.alsa_hw_auto_export);

// ---------------------------------------------------------------------------
// jsondm::ini converters for settings-specific types
// ---------------------------------------------------------------------------

namespace jsondm {
namespace ini {

template <> inline std::regex to_value<std::regex>(std::string_view v) {
  return std::regex(std::string(v));
}
// std::regex has no pattern accessor; the writer emits an empty pattern.
template <> inline std::string to_text<std::regex>(const std::regex &) {
  return "";
}

template <>
inline rtpmidid::logger_level_t
to_value<rtpmidid::logger_level_t>(std::string_view v) {
  return rtpmidid::str_to_log_level(std::string(v));
}
template <>
inline std::string
to_text<rtpmidid::logger_level_t>(const rtpmidid::logger_level_t &v) {
  switch (v) {
  case rtpmidid::logger_level_t::DEBUG:
    return "debug";
  case rtpmidid::logger_level_t::INFO:
    return "info";
  case rtpmidid::logger_level_t::WARNING:
    return "warning";
  default:
    return "error";
  }
}

template <>
inline rtpmididns::settings_t::alsa_hw_auto_export_type_e
to_value<rtpmididns::settings_t::alsa_hw_auto_export_type_e>(
    std::string_view v) {
  if (v == "none")
    return rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE;
  if (v == "hardware")
    return rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE;
  if (v == "software")
    return rtpmididns::settings_t::alsa_hw_auto_export_type_e::SOFTWARE;
  if (v == "system")
    return rtpmididns::settings_t::alsa_hw_auto_export_type_e::SYSTEM;
  if (v == "all")
    return rtpmididns::settings_t::alsa_hw_auto_export_type_e::ALL;
  throw jsondm::exception("Invalid alsa_hw_auto_export type: {}",
                          std::string(v));
}
template <>
inline std::string to_text<rtpmididns::settings_t::alsa_hw_auto_export_type_e>(
    const rtpmididns::settings_t::alsa_hw_auto_export_type_e &v) {
  switch (v) {
  case rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE:
    return "none";
  case rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE:
    return "hardware";
  case rtpmididns::settings_t::alsa_hw_auto_export_type_e::SOFTWARE:
    return "software";
  case rtpmididns::settings_t::alsa_hw_auto_export_type_e::SYSTEM:
    return "system";
  default:
    return "all";
  }
}

} // namespace ini
} // namespace jsondm
