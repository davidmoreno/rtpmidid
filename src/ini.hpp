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

#include "settings.hpp"
#include <string>
#include <unordered_map>

namespace rtpmididns {
void load_ini(const std::string &filename);

class IniReader {
  settings_t *settings;
  std::string filename;
  std::string section;
  std::string key;
  std::string value;
  int lineno = 0;

  // [peer] / [connect] / [bridge] accumulators (flushed on section change / finish)
  std::string peer_id;
  std::string peer_type;
  std::unordered_map<std::string, std::string> peer_params;
  std::string connect_from_id;
  std::string connect_to_id;
  std::unordered_map<std::string, std::string> bridge_local;
  std::unordered_map<std::string, std::string> bridge_remote;

  void flush_unified_peer();
  void flush_unified_connect();
  void flush_bridge();

public:
  IniReader(settings_t *settings) : settings(settings) {}

  void set_filename(const std::string &filename);
  void parse_line(const std::string &line);
  /** Call after last parse_line (e.g. end of file). */
  void finish();
};

} // namespace rtpmididns
