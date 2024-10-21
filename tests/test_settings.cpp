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
#include "test_case.hpp"

void test_parse_ini(void) {
  rtpmididns::settings_t settings;
  INFO("Default settings={}", settings);

  rtpmididns::IniReader reader(&settings);

  reader.parse_line("[general]");
  reader.parse_line("alsa_name=testrtpmidid");
  reader.parse_line("control=/tmp/control.sock");

  ASSERT_EQUAL(settings.alsa_name, "testrtpmidid");
  ASSERT_EQUAL(settings.control_filename, "/tmp/control.sock");

  ASSERT_EQUAL(settings.connect_to.size(), 0);
  reader.parse_line("[connect_to]");
  reader.parse_line("hostname=hostname");
  reader.parse_line("port=port");
  reader.parse_line("name=name");
  reader.parse_line("local_udp_port=local_udp_port");
  ASSERT_EQUAL(settings.connect_to.size(), 1);
  ASSERT_EQUAL(settings.connect_to[0].hostname, "hostname");
  ASSERT_EQUAL(settings.connect_to[0].port, "port");
  ASSERT_EQUAL(settings.connect_to[0].name, "name");
  ASSERT_EQUAL(settings.connect_to[0].local_udp_port, "local_udp_port");

  reader.parse_line("[connect_to]");
  reader.parse_line("hostname=hostname2");
  reader.parse_line("port=port2");
  reader.parse_line("name=name2");
  reader.parse_line("local_udp_port=local_udp_port2");
  ASSERT_EQUAL(settings.connect_to.size(), 2);
  ASSERT_EQUAL(settings.connect_to[1].hostname, "hostname2");
  ASSERT_EQUAL(settings.connect_to[1].port, "port2");
  ASSERT_EQUAL(settings.connect_to[1].name, "name2");
  ASSERT_EQUAL(settings.connect_to[1].local_udp_port, "local_udp_port2");

  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 0);
  reader.parse_line("[rtpmidi_announce]");
  reader.parse_line("name=name");
  reader.parse_line("port=port");
  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 1);
  ASSERT_EQUAL(settings.rtpmidi_announces[0].name, "name");
  ASSERT_EQUAL(settings.rtpmidi_announces[0].port, "port");

  reader.parse_line("[rtpmidi_announce]");
  reader.parse_line("name=name2");
  reader.parse_line("port=port2");
  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 2);
  ASSERT_EQUAL(settings.rtpmidi_announces[1].name, "name2");
  ASSERT_EQUAL(settings.rtpmidi_announces[1].port, "port2");

  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, true);
  bool matches = std::regex_search(
      "anything", settings.rtpmidi_discover.name_positive_regex);
  ASSERT_TRUE(matches);
  matches = std::regex_search("nothing",
                              settings.rtpmidi_discover.name_negative_regex);
  ASSERT_FALSE(matches);
  reader.parse_line("[rtpmidi_discover]");
  reader.parse_line("enabled=false");
  reader.parse_line("name_positive_regex=server:port/device");
  reader.parse_line("name_negative_regex=.*");
  matches = std::regex_search("anything",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_FALSE(matches);
  matches = std::regex_search("server:port/device",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_TRUE(matches);
  matches = std::regex_search("nothing",
                              settings.rtpmidi_discover.name_negative_regex);
  ASSERT_TRUE(matches);

  // Repeat overwrites
  reader.parse_line("name_positive_regex=mydevice");
  matches = std::regex_search("server:port/mydevice",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_TRUE(matches);
  matches = std::regex_search("server:port/device",
                              settings.rtpmidi_discover.name_positive_regex);
  ASSERT_FALSE(matches);

  reader.parse_line("[alsa_hw_auto_export]");
  reader.parse_line("type=none");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::NONE);
  reader.parse_line("type=hardware");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::HARDWARE);
  reader.parse_line("type=software");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::SOFTWARE);
  reader.parse_line("type=all");
  ASSERT_EQUAL(settings.alsa_hw_auto_export.type,
               rtpmididns::settings_t::alsa_hw_auto_export_type_e::ALL);

  reader.parse_line("name_positive_regex=(mydevice|otherdevice)");
  matches = std::regex_search(
      "mydevice", settings.alsa_hw_auto_export.name_positive_regex.value());
  ASSERT_TRUE(matches);
  reader.parse_line("name_negative_regex=(mydevice|otherdevice)");
  matches = std::regex_search(
      "mydevice", settings.alsa_hw_auto_export.name_negative_regex.value());
  ASSERT_TRUE(matches);

  ASSERT_EQUAL(settings.rawmidi.size(), 0);
  reader.parse_line("[rawmidi]");
  reader.parse_line("device=device");
  reader.parse_line("name=name");
  reader.parse_line("local_udp_port=local_udp_port");
  reader.parse_line("remote_udp_port=remote_udp_port");
  reader.parse_line("hostname=hostname");
  ASSERT_EQUAL(settings.rawmidi.size(), 1);
  ASSERT_EQUAL(settings.rawmidi[0].device, "device");
  ASSERT_EQUAL(settings.rawmidi[0].name, "name");
  ASSERT_EQUAL(settings.rawmidi[0].local_udp_port, "local_udp_port");
  ASSERT_EQUAL(settings.rawmidi[0].remote_udp_port, "remote_udp_port");
  ASSERT_EQUAL(settings.rawmidi[0].hostname, "hostname");

  reader.parse_line("[rawmidi]");
  reader.parse_line("device=device2");
  reader.parse_line("name=name2");
  reader.parse_line("local_udp_port=local_udp_port2");
  reader.parse_line("remote_udp_port=remote_udp_port2");
  reader.parse_line("hostname=hostname2");
  ASSERT_EQUAL(settings.rawmidi.size(), 2);
  ASSERT_EQUAL(settings.rawmidi[1].device, "device2");
  ASSERT_EQUAL(settings.rawmidi[1].name, "name2");
  ASSERT_EQUAL(settings.rawmidi[1].local_udp_port, "local_udp_port2");
  ASSERT_EQUAL(settings.rawmidi[1].remote_udp_port, "remote_udp_port2");
  ASSERT_EQUAL(settings.rawmidi[1].hostname, "hostname2");
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_parse_ini)};

  testcase.run(argc, argv);
  return testcase.exit_code();
}