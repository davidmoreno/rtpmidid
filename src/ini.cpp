/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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

#include "ini.hpp"
#include "settings.hpp"
#include "settings_jsondm.hpp"
#include "stringpp.hpp"
#include <fstream>
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/jsondm.hpp>
#include <rtpmidid/logger.hpp>
#include <unistd.h>

namespace rtpmididns {

// Loads an INI file and sets the data in the settings_t struct.
// Pipeline: read raw text -> fill template placeholders ({{hostname}})
// -> parse with the generated INI deserializer.
void load_ini(const std::string &filename) {
  auto fd = std::ifstream(filename);
  if (!fd.is_open()) {
    throw rtpmidid::exception("Cannot open ini file: {}", filename);
  }
  std::string text((std::istreambuf_iterator<char>(fd)),
                   std::istreambuf_iterator<char>());
  std::string filled = jsondm::fill_hostname(text);
  jsondm::IniReader reader(filled, filename);
  jsondm::ini_deserializer<settings_t>::read(reader, settings);
}

} // namespace rtpmididns
