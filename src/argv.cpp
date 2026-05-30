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
#include "rtpmidid/logger.hpp"
#include "settings.hpp"
#include "stringpp.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <rtpmidid/exceptions.hpp>
#include <string>
#include <cstdlib>
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

static settings_t::ini_peer_t *
find_ini_peer_by_prefix(settings_t *settings, std::string_view prefix) {
  for (auto &p : settings->ini_peers) {
    if (p.identity.starts_with(std::string(prefix) + ":"))
      return &p;
  }
  return nullptr;
}

static void upsert_rtpmidi_multi(settings_t *settings, const std::string &name,
                                 const std::string &port) {
  if (auto *p = find_ini_peer_by_prefix(settings, "rtpmidi_multi")) {
    if (auto id = device_identity_t::parse(p->identity)) {
      std::vector<device_identity_field_t> fields;
      fields.push_back({"name", name, false});
      if (!port.empty())
        fields.push_back({"port", port, false});
      id->fields = std::move(fields);
      p->identity = id->serialize();
      return;
    }
  }
  settings->ini_peers.push_back(
      {FMT::format("rtpmidi_multi:name={},port={}", name, port)});
}

static void upsert_alsa_multi(settings_t *settings, const std::string &name) {
  if (auto *p = find_ini_peer_by_prefix(settings, "alsa_multi")) {
    p->identity = FMT::format("alsa_multi:name={}", name);
    return;
  }
  settings->ini_peers.push_back({FMT::format("alsa_multi:name={}", name)});
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
                           const std::string name =
                               find_ini_peer_by_prefix(settings, "rtpmidi_multi")
                                   ? device_identity_t::parse(find_ini_peer_by_prefix(
                                         settings, "rtpmidi_multi")->identity)
                                         ->find("name")
                                         .value_or(get_hostname())
                                   : get_hostname();
                           upsert_rtpmidi_multi(settings, name, value);
                         });
  arguments.emplace_back( //
      "--name",           //
      "Forces the alsa and rtpmidi name", [settings](const std::string &value) {
        upsert_rtpmidi_multi(settings, value, "5004");
        upsert_alsa_multi(settings, value);
        settings->alsa_name = value;
      });
  arguments.emplace_back( //
      "--alsa-name",      //
      "Forces the alsa name", [settings](const std::string &value) {
        upsert_alsa_multi(settings, value);
      });
  arguments.emplace_back( //
      "--rtpmidid-name",  //
      "Forces the rtpmidi name", [settings](const std::string &value) {
        upsert_rtpmidi_multi(settings, value, "5004");
      });
  arguments.emplace_back("--control",
                         "Creates a control socket. Check CONTROL.md. Default "
                         "`/var/run/rtpmidid/control.sock`",
                         [settings](const std::string &value) {
                           settings->control_filename = value;
                         });
  arguments.emplace_back("--log-level",
                         "Set log level: debug, info, warning, error (or 0-3). "
                         "Default: info",
                         [settings](const std::string &value) {
                           settings->log_level = rtpmidid::str_to_log_level(value);
                         });
  arguments.emplace_back(
      "--web-disable", "Disable the web UI (HTTP + WebSocket control)",
      [settings](const std::string &) { settings->web.enabled = false; }, false);
  arguments.emplace_back("--web-listen",
                         "Web UI bind address (default 127.0.0.1)",
                         [settings](const std::string &value) {
                           settings->web.listen = value;
                         });
  arguments.emplace_back(
      "--web-port", "Web UI TCP port (default 8089)",
      [settings](const std::string &value) {
        settings->web.port = std::atoi(value.c_str());
      });
  arguments.emplace_back("--web-root",
                         "Directory with built web UI (default frontend/dist)",
                         [settings](const std::string &value) {
                           settings->web.root = value;
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
        settings->ini_peers.push_back(
            {FMT::format("rawmidi:device={}", value)});
      });
  arguments.emplace_back( //
      "--version",        //
      "Show version",
      [](const std::string &) {
        std::print("rtpmidid version {}\n", VERSION);
        exit(0);
      },
      false);
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

  if (settings->web.root.empty()) {
    settings->web.root = "frontend/dist";
  }

  DEBUG("settings after argument parsing: {}", *settings);
}

} // namespace rtpmididns
