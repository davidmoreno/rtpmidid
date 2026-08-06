/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2024 David Moreno Montero <dmoreno@coralbits.com>
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
#include "test_case.hpp"
#include <argv.hpp>
#include <rtpmidid/jsondm.hpp>
#include <unistd.h>

void test_parse_ini(void) {
  auto parse = [](const std::string &cfg) {
    rtpmididns::settings_t s;
    std::string text = cfg; // must outlive the reader (string_views)
    jsondm::IniReader r(text, "test.ini");
    jsondm::ini_deserializer<rtpmididns::settings_t>::read(r, s);
    return s;
  };

  rtpmididns::settings_t settings;
  INFO("Default settings={}", settings);

  settings =
      parse("[general]\nalsa_name=testrtpmidid\ncontrol=/tmp/control.sock\n");
  ASSERT_EQUAL(settings.alsa_name, "testrtpmidid");
  ASSERT_EQUAL(settings.control, "/tmp/control.sock");
  ASSERT_EQUAL(settings.connect_to.size(), 0);

  // repeated sections -> vector entries in file order
  settings = parse("[connect_to]\nhostname=hostname\nport=port\nname=name\n"
                   "local_udp_port=local_udp_port\n"
                   "[connect_to]\nhostname=hostname2\nport=port2\nname=name2\n"
                   "local_udp_port=local_udp_port2\n");
  ASSERT_EQUAL(settings.connect_to.size(), 2);
  ASSERT_EQUAL(settings.connect_to[0].hostname, "hostname");
  ASSERT_EQUAL(settings.connect_to[0].port, "port");
  ASSERT_EQUAL(settings.connect_to[0].name, "name");
  ASSERT_EQUAL(settings.connect_to[0].local_udp_port, "local_udp_port");
  ASSERT_EQUAL(settings.connect_to[1].hostname, "hostname2");
  ASSERT_EQUAL(settings.connect_to[1].port, "port2");
  ASSERT_EQUAL(settings.connect_to[1].name, "name2");
  ASSERT_EQUAL(settings.connect_to[1].local_udp_port, "local_udp_port2");

  settings = parse("[rtpmidi_announce]\nname=name\nport=port\n"
                   "[rtpmidi_announce]\nname=name2\nport=port2\n");
  ASSERT_EQUAL(settings.rtpmidi_announce.size(), 2);
  ASSERT_EQUAL(settings.rtpmidi_announce[0].name, "name");
  ASSERT_EQUAL(settings.rtpmidi_announce[0].port, "port");
  ASSERT_EQUAL(settings.rtpmidi_announce[1].name, "name2");
  ASSERT_EQUAL(settings.rtpmidi_announce[1].port, "port2");

  // {{hostname}} placeholder filled before parsing
  std::string with_placeholder =
      "[rtpmidi_announce]\nname={{hostname}}\nport=5004\n";
  settings = parse(jsondm::fill_hostname(with_placeholder));
  ASSERT_EQUAL(settings.rtpmidi_announce.size(), 1);
  ASSERT_TRUE(settings.rtpmidi_announce[0].name != "{{hostname}}");

  // defaults + regex members
  settings = parse("");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, true);
  bool matches = std::regex_search(
      "anything", settings.rtpmidi_discover.name_positive_regex);
  ASSERT_TRUE(matches);
  settings = parse("[rtpmidi_discover]\nenabled=false\n"
                   "name_positive_regex=server:port/device\n"
                   "name_negative_regex=.*\n");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, false);
  matches = std::regex_search("anything",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_FALSE(matches);
  matches = std::regex_search("server:port/device",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_TRUE(matches);
  matches = std::regex_search("nothing",
                              settings.rtpmidi_discover.name_negative_regex);
  ASSERT_TRUE(matches);

  // unique section: repeated occurrences overwrite (last wins)
  settings = parse("[alsa_hw_auto_export]\ntype=none\n");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE);
  settings = parse("[alsa_hw_auto_export]\ntype=hardware\n"
                   "name_positive_regex=(mydevice|otherdevice)\n"
                   "name_negative_regex=(mydevice|otherdevice)\n");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE);
  matches = std::regex_search(
      "mydevice", settings.alsa_hw_auto_export.name_positive_regex.value());
  ASSERT_TRUE(matches);
  matches = std::regex_search(
      "mydevice", settings.alsa_hw_auto_export.name_negative_regex.value());
  ASSERT_TRUE(matches);

  settings = parse("[rawmidi]\ndevice=device\nname=name\n"
                   "local_udp_port=local_udp_port\n"
                   "remote_udp_port=remote_udp_port\nhostname=hostname\n"
                   "[rawmidi]\ndevice=device2\nname=name2\n"
                   "local_udp_port=local_udp_port2\n"
                   "remote_udp_port=remote_udp_port2\nhostname=hostname2\n");
  ASSERT_EQUAL(settings.rawmidi.size(), 2);
  ASSERT_EQUAL(settings.rawmidi[0].device, "device");
  ASSERT_EQUAL(settings.rawmidi[1].name, "name2");
  ASSERT_EQUAL(settings.rawmidi[1].remote_udp_port, "remote_udp_port2");

  // invalid keys / sections throw with file:line context
  bool threw = false;
  try {
    parse("[general]\nnot_a_key=1\n");
  } catch (const jsondm::exception &e) {
    threw = true;
    ASSERT_TRUE(std::string(e.what()).find("test.ini") != std::string::npos);
  }
  ASSERT_TRUE(threw);
  threw = false;
  try {
    parse("[bogus_section]\nx=1\n");
  } catch (const jsondm::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  // bool values are strict: only 'true' or 'false' (fail loud on typos)
  settings = parse("[rtpmidi_discover]\nenabled=true\n");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, true);
  for (auto bad : {"yes", "1", "tru", "FALSE", ""}) {
    threw = false;
    try {
      parse(std::string("[rtpmidi_discover]\nenabled=") + bad + "\n");
    } catch (const jsondm::exception &e) {
      threw = true;
      ASSERT_TRUE(std::string(e.what()).find("bool") != std::string::npos);
    }
    ASSERT_TRUE(threw);
  }

  // writer round-trip (regex patterns cannot be extracted from std::regex,
  // so the round trip covers the writable members)
  settings = parse("[general]\nalsa_name=rtpmidid\nalsa_network=true\n"
                   "control=/tmp/control.sock\nlog_level=info\n"
                   "[rtpmidi_announce]\nname=n\nport=5004\n");
  std::string out;
  jsondm::IniWriter w(out);
  jsondm::ini_serializer<rtpmididns::settings_t>::write(w, settings);
  auto settings2 = parse(out);
  ASSERT_EQUAL(settings2.alsa_name, "rtpmidid");
  ASSERT_EQUAL(settings2.control, "/tmp/control.sock");
  ASSERT_EQUAL(settings2.rtpmidi_announce.size(), 1);
  ASSERT_EQUAL(settings2.rtpmidi_announce[0].port, "5004");
}

void test_load_default_ini(void) {
  // Real config: write default.ini content and load it through load_ini.
  char fn[] = "/tmp/rtpmidid_ini_test_XXXXXX";
  int fd = mkstemp(fn);
  ASSERT_TRUE(fd >= 0);
  std::string cfg = R"(# example ini file
[general]
alsa_name=rtpmidid
control=/tmp/rtpmidid_test.sock
log_level=info

[rtpmidi_announce]
name={{hostname}}
port=5004

[rtpmidi_discover]
enabled=true
name_negative_regex=^$
name_positive_regex=.*

[alsa_announce]
name=Network Export

[alsa_hw_auto_export]
name_positive_regex=.*
name_negative_regex=(System|Timer|Announce)
type=hardware
)";
  ASSERT_EQUAL(write(fd, cfg.data(), cfg.size()), (ssize_t)cfg.size());
  close(fd);
  rtpmididns::settings_t saved = rtpmididns::settings;
  rtpmididns::load_ini(fn);
  ASSERT_EQUAL(rtpmididns::settings.alsa_name, "rtpmidid");
  ASSERT_EQUAL(rtpmididns::settings.control, "/tmp/rtpmidid_test.sock");
  ASSERT_EQUAL(rtpmididns::settings.rtpmidi_announce.size(), 1);
  ASSERT_TRUE(rtpmididns::settings.rtpmidi_announce[0].name != "{{hostname}}");
  ASSERT_EQUAL(rtpmididns::settings.rtpmidi_announce[0].port, "5004");
  ASSERT_EQUAL(rtpmididns::settings.rtpmidi_discover.enabled, true);
  ASSERT_EQUAL(rtpmididns::settings.alsa_announce.size(), 1);
  ASSERT_EQUAL(rtpmididns::settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE);
  rtpmididns::settings = saved;
  unlink(fn);
}

void test_argv(void) {
  rtpmididns::settings_t settings;
  rtpmididns::parse_argv(
      {
          "--control=test.ini",
          "--rtpmidi-discover=false",
          "--port",
          "1234",
      },
      &settings);
  INFO("settings={}", settings);
  ASSERT_EQUAL(settings.control, "test.ini");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, false);
  ASSERT_EQUAL(settings.rtpmidi_announce.size(), 1);
  ASSERT_EQUAL(settings.rtpmidi_announce[0].port, "1234");

  rtpmididns::parse_argv(
      {
          "--version",
      },
      &settings);

  settings.rawmidi.clear();

  // Bugfixes
  rtpmididns::parse_argv(
      {
          "--rawmidi=",
      },
      &settings);
  ASSERT_EQUAL(settings.rawmidi.size(), 0);
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_parse_ini), TEST(test_load_default_ini),
                       TEST(test_argv)};

  testcase.run(argc, argv);
  return testcase.exit_code();
}