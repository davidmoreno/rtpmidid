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

namespace rtpmididns {
void load_ini(const std::string &filename);

class IniReader {
  settings_t *settings;
  std::string filename;
  std::string section;
  std::string key;
  std::string value;
  int lineno = 0;

  // Currently open sections, as they can appear many times
  settings_t::rtpmidi_announce_t *rtpmidi_announce = nullptr;
  settings_t::alsa_announce_t *alsa_announce = nullptr;
  settings_t::connect_to_t *connect_to = nullptr;
  settings_t::rawmidi_t *rawmidi = nullptr;

public:
  IniReader(settings_t *settings) : settings(settings) {}

  void set_filename(const std::string &filename);
  void parse_line(const std::string &line);
};

} // namespace rtpmididns