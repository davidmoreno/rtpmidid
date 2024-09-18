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

#include <fmt/core.h>
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
    connect_to_t connect_to;
  };

  std::vector<rawmidi_t> rawmidi;
};

extern settings_t settings; // NOLINT
} // namespace rtpmididns

template <>
struct fmt::formatter<rtpmididns::settings_t::alsa_announce_t>
    : formatter<fmt::string_view> {
  fmt::appender format(const rtpmididns::settings_t::alsa_announce_t &data,
                       format_context &ctx) const;
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::alsa_announce_t>>
    : formatter<fmt::string_view> {
  fmt::appender
  format(const std::vector<rtpmididns::settings_t::alsa_announce_t> &data,
         format_context &ctx) const;
};

template <>
struct fmt::formatter<rtpmididns::settings_t::rtpmidi_announce_t>
    : formatter<fmt::string_view> {
  fmt::appender format(const rtpmididns::settings_t::rtpmidi_announce_t &data,
                       format_context &ctx) const;
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::rtpmidi_announce_t>>
    : formatter<fmt::string_view> {
  fmt::appender
  format(const std::vector<rtpmididns::settings_t::rtpmidi_announce_t> &data,
         format_context &ctx) const;
};

template <>
struct fmt::formatter<rtpmididns::settings_t::connect_to_t>
    : formatter<fmt::string_view> {
  fmt::appender format(const rtpmididns::settings_t::connect_to_t &data,
                       format_context &ctx) const;
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::connect_to_t>>
    : formatter<fmt::string_view> {
  fmt::appender
  format(const std::vector<rtpmididns::settings_t::connect_to_t> &data,
         format_context &ctx) const;
};

template <>
struct fmt::formatter<rtpmididns::settings_t> : formatter<fmt::string_view> {
  fmt::appender format(const rtpmididns::settings_t &data,
                       format_context &ctx) const;
};

template <>
struct fmt::formatter<rtpmididns::settings_t::alsa_hw_auto_export_t>
    : formatter<fmt::string_view> {
  fmt::appender
  format(const rtpmididns::settings_t::alsa_hw_auto_export_t &data,
         format_context &ctx) const;
};

template <>
struct fmt::formatter<rtpmididns::settings_t::alsa_hw_auto_export_type_e>
    : formatter<fmt::string_view> {
  auto format(const rtpmididns::settings_t::alsa_hw_auto_export_type_e &data,
              format_context &ctx) const;
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::rawmidi_t>>
    : formatter<fmt::string_view> {
  auto format(const std::vector<rtpmididns::settings_t::rawmidi_t> &data,
              format_context &ctx) const;
};
