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
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include <algorithm>
#include <fmt/core.h>
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

static std::string get_hostname() {
  char hostname[256];
  gethostname(hostname, std::size(hostname));
  return hostname;
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
                             settings.rtpmidi_announces.begin()->name =
                                 get_hostname();
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
  // Necesary for two part arguments
  argument_t *current_argument = nullptr;

  for (int cargc = 1; cargc < argc; cargc++) {
    auto parsed = false;
    auto key = std::string(argv[cargc]);
    if (current_argument && current_argument->has_second_argument) {
      current_argument->fn(key);
      parsed = true;
      current_argument = nullptr;
    } else {
      // Checks all arguments
      for (auto &argument : arguments) {
        if (argument.has_second_argument) {
          auto keyeq = fmt::format("{}=", argument.arg);
          if (key.substr(0, keyeq.length()) == keyeq) {
            argument.fn(key.substr(keyeq.length()));
            parsed = true;
            break;
          }
        }
        if (key == argument.arg) {
          if (argument.has_second_argument) {
            current_argument = &argument;
            parsed = true;
          } else {
            argument.fn("");
            parsed = true;
          }
          break;
        }
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
    settings.rtpmidid_name = get_hostname();
  }
  DEBUG("settings after argument parsing: {}", settings);
}

} // namespace rtpmididns
