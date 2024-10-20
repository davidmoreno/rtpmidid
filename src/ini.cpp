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

#include "settings.hpp"
#include "stringpp.hpp"
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
  settings_t::rawmidi_t *rawmidi = nullptr;

  int lineno = 0;
  while (std::getline(fd, line)) {
    lineno++;
    // Remove comments
    auto comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }
    // Remove spaces at the beginning and end
    line = trim_copy(line);

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

      // sections that are unique, can not be repeated
      if (section == "general") {
        continue;
      } else if (section == "alsa_hw_auto_export") {
        continue;
      } else if (section == "rtpmidi_discover") {
        continue;
      } else {
        // Sections that can be repeated
        if (section == "rtpmidi_announce") {
          settings.rtpmidi_announces.emplace_back();
          rtpmidi_announce = settings.rtpmidi_announces.data() +
                             settings.rtpmidi_announces.size() - 1;
        } else if (section == "alsa_announce") {
          settings.alsa_announces.emplace_back();
          alsa_announce = settings.alsa_announces.data() +
                          settings.alsa_announces.size() - 1;
        } else if (section == "connect_to") {
          settings.connect_to.emplace_back();
          connect_to =
              settings.connect_to.data() + settings.connect_to.size() - 1;
        } else if (section == "rawmidi") {
          settings.rawmidi.emplace_back();
          rawmidi = settings.rawmidi.data() + settings.rawmidi.size() - 1;
        } else {
          throw rtpmidid::exception("Invalid section: {}", section);
        }
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
    trim(value);
    trim(key);

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
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "rtpmidi_announce") {
      if (key == "name") {
        rtpmidi_announce->name = value;
      } else if (key == "port") {
        rtpmidi_announce->port = value;
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "rtpmidi_discover") {
      if (key == "enabled") {
        settings.rtpmidi_discover.enabled = value == "true";
      } else if (key == "name_positive_regex") {
        settings.rtpmidi_discover.name_positive_regex = std::regex(value);
      } else if (key == "name_negative_regex") {
        settings.rtpmidi_discover.name_negative_regex = std::regex(value);
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "alsa_announce") {
      if (key == "name") {
        alsa_announce->name = value;
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "connect_to") {
      if (key == "hostname") {
        connect_to->hostname = value;
      } else if (key == "port") {
        connect_to->port = value;
      } else if (key == "name") {
        connect_to->name = value;
      } else if (key == "local_udp_port") {
        connect_to->local_udp_port = value;
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "alsa_hw_auto_export") {
      if (key == "type") {
        if (value == "none") {
          settings.alsa_hw_auto_export.type =
              settings_t::alsa_hw_auto_export_type_e::NONE;
        } else if (value == "hardware") {
          settings.alsa_hw_auto_export.type =
              settings_t::alsa_hw_auto_export_type_e::HARDWARE;
        } else if (value == "software") {
          settings.alsa_hw_auto_export.type =
              settings_t::alsa_hw_auto_export_type_e::SOFTWARE;
        } else if (value == "system") {
          settings.alsa_hw_auto_export.type =
              settings_t::alsa_hw_auto_export_type_e::SYSTEM;
        } else if (value == "all") {
          settings.alsa_hw_auto_export.type =
              settings_t::alsa_hw_auto_export_type_e::ALL;
        } else {
          throw rtpmidid::ini_exception(filename, lineno, "Invalid value: {}",
                                        value);
        }
      } else if (key == "name_positive_regex") {
        settings.alsa_hw_auto_export.name_positive = value;
        settings.alsa_hw_auto_export.name_positive_regex.emplace(value);
      } else if (key == "name_negative_regex") {
        settings.alsa_hw_auto_export.name_negative = value;
        settings.alsa_hw_auto_export.name_negative_regex.emplace(value);
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else if (section == "rawmidi") {
      if (key == "device") {
        rawmidi->device = value;
      } else if (key == "name") {
        rawmidi->name = value;
      } else if (key == "hostname") {
        rawmidi->hostname = value;
      } else if (key == "remote_udp_port") {
        rawmidi->remote_udp_port = value;
      } else if (key == "local_udp_port") {
        rawmidi->local_udp_port = value;
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
      }
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid section: {}",
                                    section);
    }
  }
}

} // namespace rtpmididns
