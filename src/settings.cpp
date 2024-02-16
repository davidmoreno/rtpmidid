/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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

#include "settings.hpp"

fmt::appender fmt::formatter<rtpmididns::settings_t::alsa_announce_t>::format(
    const rtpmididns::settings_t::alsa_announce_t &data, format_context &ctx) {

  return fmt::format_to(ctx.out(), "[alsa_announce_t {}]", data.name);
}

fmt::appender
fmt::formatter<std::vector<rtpmididns::settings_t::alsa_announce_t>>::format(
    const std::vector<rtpmididns::settings_t::alsa_announce_t> &data,
    format_context &ctx) {
  std::string result = "[";
  for (auto &item : data) {
    result += fmt::format("[{}] ", item.name);
  }
  result += "]";
  return fmt::format_to(ctx.out(), "{}", result);
}

fmt::appender
fmt::formatter<rtpmididns::settings_t::rtpmidi_announce_t>::format(
    const rtpmididns::settings_t::rtpmidi_announce_t &data,
    format_context &ctx) {

  return fmt::format_to(ctx.out(), "[rtpmidi_announce_t {} {}]", data.name,
                        data.port);
}

fmt::appender
fmt::formatter<std::vector<rtpmididns::settings_t::rtpmidi_announce_t>>::format(
    const std::vector<rtpmididns::settings_t::rtpmidi_announce_t> &data,
    format_context &ctx) {
  std::string result = "[";
  for (auto &item : data) {
    result += fmt::format("[rtpmidi_announce_t {} {}] ", item.name, item.port);
  }
  result += "]";
  return fmt::format_to(ctx.out(), "{}", result);
}

fmt::appender fmt::formatter<rtpmididns::settings_t::connect_to_t>::format(
    const rtpmididns::settings_t::connect_to_t &data, format_context &ctx) {

  return fmt::format_to(ctx.out(), "[connect_to_t {} {} {}]", data.hostname,
                        data.port, data.name);
}

fmt::appender
fmt::formatter<std::vector<rtpmididns::settings_t::connect_to_t>>::format(
    const std::vector<rtpmididns::settings_t::connect_to_t> &data,
    format_context &ctx) {
  std::string result = "[";
  for (auto &item : data) {
    result += fmt::format("[connect_to_t {} {} {}] ", item.hostname, item.port,
                          item.name);
  }
  result += "]";
  return fmt::format_to(ctx.out(), "{}", result);
}

fmt::appender fmt::formatter<rtpmididns::settings_t>::format(
    const rtpmididns::settings_t &data, format_context &ctx) {
#if FMT_VERSION > 90000
  return fmt::format_to(ctx.out(),
                        "[settings_t: alsa_name: {}, alsa_network: {}, "
                        "control_filename: {}, rtpmidi_announces: {}, "
                        "alsa_announces: {}, connect_to: {}, "
                        "alsa_hw_auto_export: {}]",
                        data.alsa_name, data.alsa_network,
                        data.control_filename, data.rtpmidi_announces,
                        data.alsa_announces, data.connect_to,
                        data.alsa_hw_auto_export);
#else
  return fmt::format_to(ctx.out(), "[settings_t]");
#endif
}

fmt::appender
fmt::formatter<rtpmididns::settings_t::alsa_hw_auto_export_t>::format(
    const rtpmididns::settings_t::alsa_hw_auto_export_t &data,
    format_context &ctx) {
  std::string result = "[";
  if (data.name_positive_regex.has_value()) {
    result += fmt::format("name_positive_regex: {} ", data.name_positive);
  }
  if (data.name_negative_regex.has_value()) {
    result += fmt::format("name_negative_regex: {} ", data.name_negative);
  }
  result += fmt::format("type: {} ", data.type);
  result += "]";
  return fmt::format_to(ctx.out(), "{}", result);
}

fmt::appender
fmt::formatter<rtpmididns::settings_t::alsa_hw_auto_export_type_e>::format(
    const rtpmididns::settings_t::alsa_hw_auto_export_type_e &data,
    format_context &ctx) {
  std::string result = "[";
  if (data == rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE) {
    result += "NONE";
  } else if (data == rtpmididns::settings_t::alsa_hw_auto_export_type_e::ALL) {
    result += "ALL";
  } else {
    if (data & rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE) {
      result += "HARDWARE ";
    }
    if (data & rtpmididns::settings_t::alsa_hw_auto_export_type_e::SOFTWARE) {
      result += "SOFTWARE ";
    }
    if (data & rtpmididns::settings_t::alsa_hw_auto_export_type_e::SYSTEM) {
      result += "SYSTEM ";
    }
  }
  result += "]";
  return fmt::format_to(ctx.out(), "{}", result);
}