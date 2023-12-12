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
#include <chrono>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <vector>

namespace rtpmididns {

struct settings_t {
  std::string alsa_name = "rtpmidid";
  bool alsa_network = true;
  std::string rtpmidid_name = "";
  std::string rtpmidid_port = "5004";
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
  };

  std::vector<rtpmidi_announce_t> rtpmidi_announces;
  std::vector<alsa_announce_t> alsa_announces;
  std::vector<connect_to_t> connect_to;
};

extern settings_t settings;
} // namespace rtpmididns

template <>
struct fmt::formatter<rtpmididns::settings_t::alsa_announce_t>
    : formatter<std::string_view> {
  auto format(const rtpmididns::settings_t::alsa_announce_t &data,
              format_context &ctx) {

    return fmt::format_to(ctx.out(), "[alsa_announce_t {}]", data.name);
  }
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::alsa_announce_t>>
    : formatter<std::string_view> {
  auto format(const std::vector<rtpmididns::settings_t::alsa_announce_t> &data,
              format_context &ctx) {
    std::string result = "[";
    for (auto &item : data) {
      result += fmt::format("[{}] ", item.name);
    }
    result += "]";
    return fmt::format_to(ctx.out(), "{}", result);
  }
};

template <>
struct fmt::formatter<rtpmididns::settings_t::rtpmidi_announce_t>
    : formatter<std::string_view> {
  auto format(const rtpmididns::settings_t::rtpmidi_announce_t &data,
              format_context &ctx) {

    return fmt::format_to(ctx.out(), "[rtpmidi_announce_t {} {}]", data.name,
                          data.port);
  }
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::rtpmidi_announce_t>>
    : formatter<std::string_view> {
  auto
  format(const std::vector<rtpmididns::settings_t::rtpmidi_announce_t> &data,
         format_context &ctx) {
    std::string result = "[";
    for (auto &item : data) {
      result +=
          fmt::format("[rtpmidi_announce_t {} {}] ", item.name, item.port);
    }
    result += "]";
    return fmt::format_to(ctx.out(), "{}", result);
  }
};

template <>
struct fmt::formatter<rtpmididns::settings_t::connect_to_t>
    : formatter<std::string_view> {
  auto format(const rtpmididns::settings_t::connect_to_t &data,
              format_context &ctx) {

    return fmt::format_to(ctx.out(), "[connect_to_t {} {} {}]", data.hostname,
                          data.port, data.name);
  }
};

template <>
struct fmt::formatter<std::vector<rtpmididns::settings_t::connect_to_t>>
    : formatter<std::string_view> {
  auto format(const std::vector<rtpmididns::settings_t::connect_to_t> &data,
              format_context &ctx) {
    std::string result = "[";
    for (auto &item : data) {
      result += fmt::format("[connect_to_t {} {} {}] ", item.hostname,
                            item.port, item.name);
    }
    result += "]";
    return fmt::format_to(ctx.out(), "{}", result);
  }
};

template <>
struct fmt::formatter<rtpmididns::settings_t> : formatter<std::string_view> {
  auto format(const rtpmididns::settings_t &data, format_context &ctx) {

    return fmt::format_to(
        ctx.out(),
        "[settings_t alsa_name: {} alsa_network: {} rtpmidid_name: {} "
        "rtpmidid_port: {} control_filename: {} rtpmidi_announces: {} "
        "alsa_announces: {} connect_to: {}]",
        data.alsa_name, data.alsa_network, data.rtpmidid_name,
        data.rtpmidid_port, data.control_filename, data.rtpmidi_announces,
        data.alsa_announces, data.connect_to);
  }
};
