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

#include "ini.hpp"
#include "settings.hpp"
#include <algorithm>
#include <fstream>
#include <rtpmidid/exceptions.hpp>
#include <unistd.h>

namespace rtpmididns {

// Loads an INI file and sets the data in the settings_t struct
void load_ini(const std::string &filename) {
  auto fd = std::ifstream(filename);
  if (!fd.is_open()) {
    throw rtpmidid::exception("Cannot open ini file: {}", filename);
  }
  // Read line by line the file at fd
  std::string line;
  std::string section;
  std::string key;
  std::string value;

  // Currently open sections
  settings_t::rtpmidi_announce_t *rtpmidi_announce = nullptr;
  settings_t::alsa_announce_t *alsa_announce = nullptr;
  settings_t::connect_to_t *connect_to = nullptr;

  while (std::getline(fd, line)) {
    // Remove comments
    auto comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    // Remove spaces
    line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());
    // Skip empty lines
    if (line.length() == 0) {
      continue;
    }
    // Check if it is a section
    if (line[0] == '[') {
      if (line[line.length() - 1] != ']') {
        throw rtpmidid::exception("Invalid section: {}", line);
      }
      section = line.substr(1, line.length() - 2);

      if (section == "general") {
        continue;
      } else if (section == "rtpmidi_announce") {
        settings.rtpmidi_announces.emplace_back();
        rtpmidi_announce = settings.rtpmidi_announces.data() +
                           settings.rtpmidi_announces.size() - 1;
      } else if (section == "alsa_announce") {
        settings.alsa_announces.emplace_back();
        alsa_announce =
            settings.alsa_announces.data() + settings.alsa_announces.size() - 1;
      } else if (section == "connect_to") {
        settings.connect_to.emplace_back();
        connect_to =
            settings.connect_to.data() + settings.connect_to.size() - 1;
      } else {
        throw rtpmidid::exception("Invalid section: {}", section);
      }

      continue;
    }
    // Check if it is a key
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      throw rtpmidid::exception("Invalid line: {}", line);
    }
    key = line.substr(0, eq_pos);
    value = line.substr(eq_pos + 1);

    // If value has {{hostname}} replace it with the result of the function
    // hostname(null), beware of the double {{ }}
    if (value.find("{{hostname}}") != std::string::npos) {
      char hostname[256];
      gethostname(hostname, std::size(hostname));
      // replace the placeholder. DO NOT USE fmt::format as it will not change
      // both opening brackets, use replace_all
      std::string hostname_str = hostname;
      std::string hostname_placeholder = "{{hostname}}";
      std::string::size_type n = 0;
      while ((n = value.find(hostname_placeholder, n)) != std::string::npos) {
        value.replace(n, hostname_placeholder.size(), hostname_str);
        n += hostname_str.size();
      }
    }

    // Store the value
    if (section == "general") {
      if (key == "alsa_name") {
        settings.alsa_name = value;
      } else if (key == "control") {
        settings.control_filename = value;
      } else {
        throw rtpmidid::exception("Invalid key: {}", key);
      }
    } else if (section == "rtpmidi_announce") {
      if (key == "name") {
        rtpmidi_announce->name = value;
      } else if (key == "port") {
        rtpmidi_announce->port = value;
      } else {
        throw rtpmidid::exception("Invalid key: {}", key);
      }
    } else if (section == "alsa_announce") {
      if (key == "name") {
        alsa_announce->name = value;
      } else {
        throw rtpmidid::exception("Invalid key: {}", key);
      }
    } else if (section == "connect_to") {
      if (key == "hostname") {
        connect_to->hostname = value;
      } else if (key == "port") {
        connect_to->port = value;
      } else if (key == "name") {
        connect_to->name = value;
      } else {
        throw rtpmidid::exception("Invalid key: {}", key);
      }
    } else {
      throw rtpmidid::exception("Invalid section: {}", section);
    }
  }
}

} // namespace rtpmididns
