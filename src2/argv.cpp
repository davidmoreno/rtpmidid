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

#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <rtpmidid/exceptions.hpp>
#include <string>
#include <unistd.h>
#include <vector>

namespace rtpmididns {

#ifndef RTPMIDID_VERSION
#define RTPMIDID_VERSION "unknown"
#endif

const char *VERSION = RTPMIDID_VERSION;

const char *CMDLINE_HELP =
    ""
    "rtpmidid v{}/2\n"
    "Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.\n"
    "\n"
    "rtpmidi allows to use rtpmidi protocol to communicate with MIDI "
    "equipement \n"
    "using network equipiment. Recomended use is via ethernet cabling as with "
    "WiFi\n"
    "there is a lot more latency. Internet use has not been tested, but may "
    "also\n"
    "deliver high latency.\n"
    "\n"
    "Options:\n";

struct argument_t {
  std::string arg;
  std::string comment;
  std::function<void(const std::string &)> fn;
  bool has_second_argument = true;

  argument_t(const std::string &arg, const std::string &comment,
             std::function<void(const std::string &)> fn,
             bool has_second_argument = true)
      : arg(arg), comment(comment), fn(fn),
        has_second_argument(has_second_argument) {}
};

bool str_to_bool(const std::string &value) {
  // conver t value to lowercase, so True becomes true
  std::string value_lowercase = value;
  for (auto &c : value_lowercase) {
    c = std::tolower(c);
  }
  if (value_lowercase == "true") {
    return true;
  }
  if (value_lowercase == "false") {
    return false;
  }
  throw rtpmidid::exception("Invalid boolean value: {}", value);
}

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
      if (key == "address") {
        connect_to->address = value;
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

// Parses the argv and sets up the settings_t struct
// for parameters that affect a alsa and rtpmidi announcements, it changes the
// first announced, and creates it if needed
void parse_argv(int argc, char **argv) {
  std::vector<argument_t> arguments;

  auto help = [&] {
    fmt::print(CMDLINE_HELP, VERSION);
    for (auto &argument : arguments) {
      fmt::print("  {:<30} {}\n", argument.arg, argument.comment);
    }
  };

  arguments.emplace_back( //
      "--ini",            //
      "Loads an INI file as default configuration. Depending on order may "
      "overwrite other arguments",
      [](const std::string &value) { load_ini(value); });

  arguments.emplace_back("--port", //
                         "Opens local port as server. Default 5004.",
                         [](const std::string &value) {
                           if (settings.rtpmidi_announces.size() == 0) {
                             settings.rtpmidi_announces.emplace_back();
                           }
                           settings.rtpmidi_announces.begin()->port = value;
                         });
  arguments.emplace_back( //
      "--name",           //
      "Forces the alsa and rtpmidi name", [](const std::string &value) {
        if (settings.rtpmidi_announces.size() == 0) {
          settings.rtpmidi_announces.emplace_back();
        }
        if (settings.alsa_announces.size() == 0) {
          settings.alsa_announces.emplace_back();
        }

        settings.rtpmidi_announces.begin()->name = value;
        settings.alsa_announces.begin()->name = value;
      });
  arguments.emplace_back( //
      "--alsa-name",      //
      "Forces the alsa name", [](const std::string &value) {
        if (settings.alsa_announces.size() == 0) {
          settings.alsa_announces.emplace_back();
        }
        settings.alsa_announces.begin()->name = value;
      });
  arguments.emplace_back(
      "--alsa-network-announcement",
      "Enables / Disables the ALSA Network port to announce on the network. "
      "If you have a firewall you want this disabled and allow only "
      "connections from the network to rtpmidid, not announce new endpoints. "
      "Default: True",
      [](const std::string &value) { settings.rtpmidi_announces.clear(); });
  arguments.emplace_back( //
      "--rtpmidid-name",  //
      "Forces the rtpmidi name", [](const std::string &value) {
        if (settings.rtpmidi_announces.size() == 0) {
          settings.rtpmidi_announces.emplace_back();
        }
        settings.rtpmidi_announces.begin()->name = value;
      });
  arguments.emplace_back(
      "--control",
      "Creates a control socket. Check CONTROL.md. Default "
      "`/var/run/rtpmidid/control.sock`",
      [](const std::string &value) { settings.control_filename = value; });
  arguments.emplace_back( //
      "--version",        //
      "Show version", [](const std::string &value) {
        fmt::print("rtpmidid version {}/2\n", VERSION);
        exit(0);
      });
  arguments.emplace_back( //
      "--help",           //
      "Show this help",
      [&](const std::string &value) {
        help();
        exit(0);
      },
      false);

  auto arg = std::string(argv[0]);
  bool second_part_comming = false;
  for (int cargc = 1; cargc < argc; cargc++) {
    auto parsed = false;
    auto key = std::string(argv[cargc]);
    // Checks all arguments
    for (auto &argument : arguments) {
      if (second_part_comming) {
        argument.fn(argv[cargc]);
        second_part_comming = false;
        parsed = true;
        break;
      }
      if (argument.has_second_argument) {
        auto keyeq = fmt::format("{}=", argument.arg);
        if (key.substr(0, keyeq.length()) == keyeq) {
          second_part_comming = false;
          argument.fn(key.substr(keyeq.length()));
          parsed = true;
          break;
        }
      }
      if (key == argument.arg) {
        if (argument.has_second_argument) {
          second_part_comming = true;
          parsed = true;
        } else {
          argument.fn("");
          parsed = true;
        }
        break;
      }
    }
    // If none parsed, error
    if (!parsed) {
      ERROR("Unknown argument: {}", key);
      help();
      exit(1);
    }
  }

  if (settings.rtpmidid_name == "") {
    char hostname[256];
    gethostname(hostname, std::size(hostname));
    settings.rtpmidid_name = hostname;
  }
  DEBUG("settings after argument parsing: {}", settings);
}

} // namespace rtpmididns
