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
#include "device_identity.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <fstream>
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <unistd.h>
#include <cstdlib>

namespace rtpmididns {

void load_ini(const std::string &filename) {
  auto fd = std::ifstream(filename);
  if (!fd.is_open()) {
    throw rtpmidid::exception("Cannot open ini file: {}", filename);
  }
  auto reader = IniReader(&settings);

  reader.set_filename(filename);
  std::string line;
  while (std::getline(fd, line)) {
    reader.parse_line(line);
  }
  reader.finish();
}

void IniReader::set_filename(const std::string &filename) {
  this->filename = filename;
}

void IniReader::finish() {
  flush_peer();
  flush_connect();
}

void IniReader::flush_peer() {
  if (peer_identity.empty())
    return;
  if (!device_identity_t::parse(peer_identity)) {
    throw rtpmidid::ini_exception(filename, lineno,
                                  "[peer]: invalid identity '{}'", peer_identity);
  }
  settings->ini_peers.push_back(settings_t::ini_peer_t{peer_identity});
  peer_identity.clear();
}

void IniReader::flush_connect() {
  if (connect_from.empty() && connect_to.empty())
    return;
  if (connect_from.empty() || connect_to.empty()) {
    throw rtpmidid::ini_exception(filename, lineno,
                                  "[connect]: from and to are required");
  }
  settings_t::ini_connect_t c;
  c.from = connect_from;
  c.to = connect_to;
  if (!connect_direction.empty())
    c.direction = connect_direction;
  settings->ini_connects.push_back(std::move(c));
  connect_from.clear();
  connect_to.clear();
  connect_direction.clear();
}

void IniReader::parse_line(const std::string &origline) {
  std::string line = origline;
  lineno++;
  auto comment_pos = line.find('#');
  if (comment_pos != std::string::npos) {
    line = line.substr(0, comment_pos);
  }
  line = trim_copy(line);

  if (line.length() == 0) {
    return;
  }
  if (line[0] == '[') {
    if (line[line.length() - 1] != ']') {
      throw rtpmidid::exception("Invalid section: {}", line);
    }
    flush_peer();
    flush_connect();
    section = trim_copy(line.substr(1, line.length() - 2));

    if (section == "general" || section == "web" || section == "database" ||
        section == "alsa_hw_auto_export" || section == "rtpmidi_discover") {
      return;
    }
    if (section == "peer") {
      peer_identity.clear();
      return;
    }
    if (section == "connect") {
      connect_from.clear();
      connect_to.clear();
      connect_direction.clear();
      return;
    }
    throw rtpmidid::exception("Invalid section: {}", section);
  }

  auto eq_pos = line.find('=');
  if (eq_pos == std::string::npos) {
    throw rtpmidid::exception("Invalid line: {}", line);
  }
  key = line.substr(0, eq_pos);
  value = line.substr(eq_pos + 1);
  trim(value);
  trim(key);

  if (value.find("{{hostname}}") != std::string::npos) {
    char hostname[256];
    gethostname(hostname, std::size(hostname));
    std::string hostname_str = hostname;
    std::string hostname_placeholder = "{{hostname}}";
    std::string::size_type n = 0;
    while ((n = value.find(hostname_placeholder, n)) != std::string::npos) {
      value.replace(n, hostname_placeholder.size(), hostname_str);
      n += hostname_str.size();
    }
  }

  if (section == "general") {
    if (key == "alsa_name") {
      settings->alsa_name = value;
    } else if (key == "control") {
      settings->control_filename = value;
    } else if (key == "log_level") {
      settings->log_level = rtpmidid::str_to_log_level(value);
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "web") {
    if (key == "enabled") {
      settings->web.enabled = value == "true";
    } else if (key == "listen") {
      settings->web.listen = value;
    } else if (key == "port") {
      settings->web.port = std::atoi(value.c_str());
    } else if (key == "root") {
      settings->web.root = value;
    } else if (key == "username") {
      settings->web.username = value;
    } else if (key == "password") {
      settings->web.password = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "database") {
    if (key == "path") {
      settings->database.path = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "rtpmidi_discover") {
    if (key == "enabled") {
      settings->rtpmidi_discover.enabled = value == "true";
    } else if (key == "name_positive_regex") {
      settings->rtpmidi_discover.name_positive_regex = std::regex(value);
    } else if (key == "name_negative_regex") {
      settings->rtpmidi_discover.name_negative_regex = std::regex(value);
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "alsa_hw_auto_export") {
    if (key == "type") {
      if (value == "none") {
        settings->alsa_hw_auto_export.type =
            settings_t::alsa_hw_auto_export_type_e::NONE;
      } else if (value == "hardware") {
        settings->alsa_hw_auto_export.type =
            settings_t::alsa_hw_auto_export_type_e::HARDWARE;
      } else if (value == "software") {
        settings->alsa_hw_auto_export.type =
            settings_t::alsa_hw_auto_export_type_e::SOFTWARE;
      } else if (value == "system") {
        settings->alsa_hw_auto_export.type =
            settings_t::alsa_hw_auto_export_type_e::SYSTEM;
      } else if (value == "all") {
        settings->alsa_hw_auto_export.type =
            settings_t::alsa_hw_auto_export_type_e::ALL;
      } else {
        throw rtpmidid::ini_exception(filename, lineno, "Invalid value: {}",
                                      value);
      }
    } else if (key == "name_positive_regex") {
      settings->alsa_hw_auto_export.name_positive = value;
      settings->alsa_hw_auto_export.name_positive_regex.emplace(value);
    } else if (key == "name_negative_regex") {
      settings->alsa_hw_auto_export.name_negative = value;
      settings->alsa_hw_auto_export.name_negative_regex.emplace(value);
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "peer") {
    if (key == "identity") {
      peer_identity = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno,
                                    "[peer]: only identity= is supported");
    }
  } else if (section == "connect") {
    if (key == "from") {
      connect_from = value;
    } else if (key == "to") {
      connect_to = value;
    } else if (key == "direction") {
      connect_direction = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else {
    throw rtpmidid::ini_exception(filename, lineno, "Invalid section: {}",
                                  section);
  }
}

} // namespace rtpmididns
