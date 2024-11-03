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
#include "stringpp.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <rtpmidid/exceptions.hpp>
#include <string>
#include <unistd.h>
#include <vector>

namespace rtpmididns {

#ifndef RTPMIDID_VERSION
// NOLINTNEXTLINE
#define RTPMIDID_VERSION "unknown"
#endif

// NOLINTNEXTLINE
const char *VERSION = RTPMIDID_VERSION;

// NOLINTNEXTLINE (cppcoreguidelines-pro-bounds-pointer-arithmetic)
constexpr const char *const CMDLINE_HELP = &R"(
Real Time Protocol Music Instrument Digital Interface Daemon v{}
(C) 2019-2023 David Moreno Montero <dmoreno@coralbits.com>
Share ALSA sequencer MIDI ports using rtpmidi, and viceversa.

rtpmidi allows to use rtpmidi protocol to communicate with MIDI 
equipement using network equipiment. Recomended use is via ethernet 
cabling as with WiFi there is a lot more latency and a lot of jitter. 
Internet use has not been tested, but may also deliver high latency
and jitter.

Options:
)"[1];

struct argument_t {
  std::string arg;
  std::string comment;
  std::function<void(const std::string &)> fn;
  bool has_second_argument = true;

  // NOLINTNEXTLINE
  argument_t(const std::string &arg, const std::string &comment,
             std::function<void(const std::string &)> fn,
             bool has_second_argument = true)
      : arg(arg), comment(comment), fn(fn),
        has_second_argument(has_second_argument) {}
};

bool str_to_bool(const std::string &value) {
  // conver t value to lowercase, so True becomes true
  std::string value_lowercase;
  std::transform(value.begin(), value.end(), value_lowercase.begin(),
                 ::tolower);

  if (value_lowercase == "true") {
    return true;
  }
  if (value_lowercase == "false") {
    return false;
  }
  throw rtpmidid::exception("Invalid boolean value: {}", value);
}

static std::string get_hostname() {
  constexpr auto MAX_HOSTNAME_SIZE = 256;
  std::array<char, MAX_HOSTNAME_SIZE> hostname{0};
  hostname.fill(0);
  ::gethostname(hostname.data(), std::size(hostname));
  return std::string(hostname.data());
}

void help(const std::vector<argument_t> &arguments) {
  std::print(CMDLINE_HELP, VERSION);
  for (auto &argument : arguments) {
    std::print("  {:<30} {}\n", argument.arg, argument.comment);
  }
}

// Setup the argument options
static std::vector<argument_t> setup_arguments(settings_t *settings) {
  std::vector<argument_t> arguments;

  arguments.emplace_back( //
      "--ini",            //
      "Loads an INI file as default configuration. Depending on order may "
      "overwrite other arguments",
      [](const std::string &value) { load_ini(value); });

  arguments.emplace_back("--port", //
                         "Opens local port as server. Default 5004.",
                         [settings](const std::string &value) {
                           if (settings->rtpmidi_announces.size() == 0) {
                             settings->rtpmidi_announces.emplace_back();
                             settings->rtpmidi_announces.begin()->name =
                                 get_hostname();
                           }
                           settings->rtpmidi_announces.begin()->port = value;
                         });
  arguments.emplace_back( //
      "--name",           //
      "Forces the alsa and rtpmidi name", [settings](const std::string &value) {
        if (settings->rtpmidi_announces.size() == 0) {
          settings->rtpmidi_announces.emplace_back();
        }
        if (settings->alsa_announces.size() == 0) {
          settings->alsa_announces.emplace_back();
        }

        settings->rtpmidi_announces.begin()->name = value;
        settings->alsa_announces.begin()->name = value;
        settings->alsa_name = value;
      });
  arguments.emplace_back( //
      "--alsa-name",      //
      "Forces the alsa name", [settings](const std::string &value) {
        if (settings->alsa_announces.size() == 0) {
          settings->alsa_announces.emplace_back();
        }
        settings->alsa_announces.begin()->name = value;
      });
  arguments.emplace_back( //
      "--rtpmidid-name",  //
      "Forces the rtpmidi name", [settings](const std::string &value) {
        if (settings->rtpmidi_announces.size() == 0) {
          settings->rtpmidi_announces.emplace_back();
        }
        settings->rtpmidi_announces.begin()->name = value;
      });
  arguments.emplace_back("--control",
                         "Creates a control socket. Check CONTROL.md. Default "
                         "`/var/run/rtpmidid/control.sock`",
                         [settings](const std::string &value) {
                           settings->control_filename = value;
                         });
  arguments.emplace_back( //
      "--rtpmidi-discover",
      "Enable or disable rtpmidi discover. true | false | [posregex] | "
      "![negregex]",
      [settings](const std::string &value) {
        if (value == "true") {
          DEBUG("rtpmidi_discover.enabled = true");
          settings->rtpmidi_discover.enabled = true;
        } else if (value == "false") {
          DEBUG("rtpmidi_discover.enabled = false");
          settings->rtpmidi_discover.enabled = false;
        } else if (std::startswith(value, "!")) {
          DEBUG("rtpmidi_discover.name_negative_regex = {}", value.substr(1));
          settings->rtpmidi_discover.name_negative_regex =
              std::regex(value.substr(1, std::string::npos));
        } else {
          DEBUG("rtpmidi_discover.name_positive_regex = {}", value);
          settings->rtpmidi_discover.name_positive_regex = std::regex(value);
        }
      });
  arguments.emplace_back( //
      "--rawmidi",
      "Connects to a rawmidi device. For example `/dev/snd/midiC1D0`",
      [settings](const std::string &value) {
        if (value.size() == 0) {
          ERROR("Empty rawmidi device. Doing nothing.");
          return;
        }
        settings->rawmidi.emplace_back();
        settings->rawmidi.back().device = value;
        settings->rawmidi.back().name = rtpmididns::split(value, '/').back();
      });
  arguments.emplace_back( //
      "--version",        //
      "Show version", [settings](const std::string &value) {
        std::print("rtpmidid version {}/2\n", VERSION);
        exit(0);
      });
  arguments.emplace_back( //
      "--help",           //
      "Show this help",
      [&](const std::string &value) {
        help(arguments);
        exit(0);
      },
      false);
  return arguments;
}

// Parses the argv and sets up the settings_t struct
// for parameters that affect a alsa and rtpmidi announcements, it changes the
// first announced, and creates it if needed
void parse_argv(const std::vector<std::string> &argv, settings_t *settings) {
  std::vector<argument_t> arguments = setup_arguments(settings);
  // Necesary for two part arguments
  argument_t *current_argument = nullptr;

  for (auto &key : argv) {
    auto parsed = false;
    if (current_argument && current_argument->has_second_argument) {
      current_argument->fn(key);
      parsed = true;
      current_argument = nullptr;
    } else {
      // Checks all arguments
      for (auto &argument : arguments) {
        if (argument.has_second_argument) {
          auto keyeq = FMT::format("{}=", argument.arg);
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
      ERROR("Unknown argument: {}. Try help with --help.", key);
      exit(1);
    }
  }

  DEBUG("settings after argument parsing: {}", *settings);
}

} // namespace rtpmididns
