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
#include "ini_graph.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <fstream>
#include <rtpmidid/exceptions.hpp>
#include <rtpmidid/logger.hpp>
#include <unistd.h>
#include <cstdlib>

namespace rtpmididns {

// Loads an INI file and sets the data in the settings_t struct
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
  finalize_unified_ini_graph(settings, filename);
}

void IniReader::set_filename(const std::string &filename) {
  this->filename = filename;
}

void IniReader::finish() {
  flush_unified_peer();
  flush_unified_connect();
  flush_bridge();
}

static std::string bridge_get(const std::unordered_map<std::string, std::string> &m,
                              const std::string &k) {
  auto it = m.find(k);
  return it == m.end() ? std::string() : it->second;
}

void IniReader::flush_unified_peer() {
  if (peer_id.empty() && peer_type.empty() && peer_params.empty())
    return;
  if (peer_id.empty() || peer_type.empty()) {
    throw rtpmidid::ini_exception(filename, lineno,
                                  "[peer]: id and type are required");
  }
  settings_t::ini_peer_template_t t;
  t.id = std::move(peer_id);
  t.type = std::move(peer_type);
  t.params = std::move(peer_params);
  settings->ini_peers.push_back(std::move(t));
  peer_id.clear();
  peer_type.clear();
  peer_params.clear();
}

void IniReader::flush_unified_connect() {
  if (connect_from_id.empty() && connect_to_id.empty())
    return;
  if (connect_from_id.empty() || connect_to_id.empty()) {
    throw rtpmidid::ini_exception(filename, lineno,
                                  "[connect]: from and to are required");
  }
  settings->ini_connects.push_back(
      settings_t::ini_connect_t{connect_from_id, connect_to_id});
  connect_from_id.clear();
  connect_to_id.clear();
}

void IniReader::flush_bridge() {
  if (bridge_local.empty() && bridge_remote.empty())
    return;
  const std::string li = bridge_get(bridge_local, "id");
  const std::string lt = bridge_get(bridge_local, "type");
  const std::string ri = bridge_get(bridge_remote, "id");
  const std::string rt = bridge_get(bridge_remote, "type");
  if (li.empty() || lt.empty() || ri.empty() || rt.empty()) {
    throw rtpmidid::ini_exception(
        filename, lineno,
        "[bridge]: local.id, local.type, remote.id, remote.type are required");
  }

  auto peer_from_side = [&](const std::string &id, const std::string &type,
                            const std::unordered_map<std::string, std::string> &m) {
    settings_t::ini_peer_template_t p;
    p.id = id;
    p.type = type;
    for (const auto &e : m) {
      if (e.first != "id" && e.first != "type")
        p.params.insert(e);
    }
    return p;
  };

  if (lt == "alsa_listener" && rt == "rtpmidi_connect") {
    settings_t::connect_to_t ct;
    ct.name = bridge_get(bridge_local, "name");
    ct.hostname = bridge_get(bridge_remote, "hostname");
    ct.port = bridge_get(bridge_remote, "port");
    if (ct.port.empty())
      ct.port = bridge_get(bridge_remote, "remote_udp_port");
    ct.local_udp_port = bridge_get(bridge_remote, "local_udp_port");
    settings->connect_to.push_back(std::move(ct));
    bridge_local.clear();
    bridge_remote.clear();
    return;
  }

  if (lt == "rawmidi" && (rt == "rtpmidi_listen" || rt == "rtpmidi_connect")) {
    settings->ini_peers.push_back(peer_from_side(li, lt, bridge_local));
    settings->ini_peers.push_back(peer_from_side(ri, rt, bridge_remote));
    settings->ini_connects.push_back({li, ri});
    settings->ini_connects.push_back({ri, li});
    bridge_local.clear();
    bridge_remote.clear();
    return;
  }
  if (rt == "rawmidi" && (lt == "rtpmidi_listen" || lt == "rtpmidi_connect")) {
    settings->ini_peers.push_back(peer_from_side(ri, rt, bridge_remote));
    settings->ini_peers.push_back(peer_from_side(li, lt, bridge_local));
    settings->ini_connects.push_back({li, ri});
    settings->ini_connects.push_back({ri, li});
    bridge_local.clear();
    bridge_remote.clear();
    return;
  }

  throw rtpmidid::ini_exception(
      filename, lineno,
      "[bridge]: unsupported local.type={} remote.type={} combination", lt, rt);
}

void IniReader::parse_line(const std::string &origline) {
  std::string line = origline;
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
    return;
  }
  // Check if it is a section
  if (line[0] == '[') {
    if (line[line.length() - 1] != ']') {
      throw rtpmidid::exception("Invalid section: {}", line);
    }
    flush_unified_peer();
    flush_unified_connect();
    flush_bridge();
    section = trim_copy(line.substr(1, line.length() - 2));

    // sections that are unique, can not be repeated
    if (section == "general") {
      return;
    } else if (section == "web") {
      return;
    } else if (section == "alsa_hw_auto_export") {
      return;
    } else if (section == "rtpmidi_discover") {
      return;
    } else {
      // Sections that can be repeated
      if (section == "peer") {
        peer_id.clear();
        peer_type.clear();
        peer_params.clear();
        return;
      } else if (section == "connect") {
        connect_from_id.clear();
        connect_to_id.clear();
        return;
      } else if (section == "bridge") {
        bridge_local.clear();
        bridge_remote.clear();
        return;
      } else {
        throw rtpmidid::exception("Invalid section: {}", section);
      }
    }

    return;
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
    if (key == "id") {
      peer_id = value;
    } else if (key == "type") {
      peer_type = value;
    } else {
      peer_params[key] = value;
    }
  } else if (section == "connect") {
    if (key == "from") {
      connect_from_id = value;
    } else if (key == "to") {
      connect_to_id = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else if (section == "bridge") {
    static const char local_p[] = "local.";
    static const char remote_p[] = "remote.";
    const std::size_t ll = sizeof(local_p) - 1;
    const std::size_t lr = sizeof(remote_p) - 1;
    if (key.size() > ll && key.compare(0, ll, local_p) == 0) {
      bridge_local[key.substr(ll)] = value;
    } else if (key.size() > lr && key.compare(0, lr, remote_p) == 0) {
      bridge_remote[key.substr(lr)] = value;
    } else {
      throw rtpmidid::ini_exception(filename, lineno, "Invalid key: {}", key);
    }
  } else {
    throw rtpmidid::ini_exception(filename, lineno, "Invalid section: {}",
                                  section);
  }
}

} // namespace rtpmididns
