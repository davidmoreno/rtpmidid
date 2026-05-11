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
#include "ini_graph.hpp"
#include "settings.hpp"
#include "test_case.hpp"
#include <argv.hpp>

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
  reader.parse_line("[bridge]");
  reader.parse_line("local.id=alsa_ct1");
  reader.parse_line("local.type=alsa_listener");
  reader.parse_line("local.name=name");
  reader.parse_line("remote.id=rtp_ct1");
  reader.parse_line("remote.type=rtpmidi_connect");
  reader.parse_line("remote.hostname=hostname");
  reader.parse_line("remote.port=port");
  reader.parse_line("remote.local_udp_port=local_udp_port");
  reader.parse_line("[bridge]");
  ASSERT_EQUAL(settings.connect_to.size(), 1);
  ASSERT_EQUAL(settings.connect_to[0].hostname, "hostname");
  ASSERT_EQUAL(settings.connect_to[0].port, "port");
  ASSERT_EQUAL(settings.connect_to[0].name, "name");
  ASSERT_EQUAL(settings.connect_to[0].local_udp_port, "local_udp_port");

  reader.parse_line("local.id=alsa_ct2");
  reader.parse_line("local.type=alsa_listener");
  reader.parse_line("local.name=name2");
  reader.parse_line("remote.id=rtp_ct2");
  reader.parse_line("remote.type=rtpmidi_connect");
  reader.parse_line("remote.hostname=hostname2");
  reader.parse_line("remote.port=port2");
  reader.parse_line("remote.local_udp_port=local_udp_port2");
  reader.parse_line("[peer]");
  ASSERT_EQUAL(settings.connect_to.size(), 2);
  ASSERT_EQUAL(settings.connect_to[1].hostname, "hostname2");
  ASSERT_EQUAL(settings.connect_to[1].port, "port2");
  ASSERT_EQUAL(settings.connect_to[1].name, "name2");
  ASSERT_EQUAL(settings.connect_to[1].local_udp_port, "local_udp_port2");

  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 0);
  reader.parse_line("id=ann1");
  reader.parse_line("type=listen_rtpmidi");
  reader.parse_line("name=name");
  reader.parse_line("port=port");
  reader.parse_line("[peer]");
  reader.parse_line("id=ann2");
  reader.parse_line("type=listen_rtpmidi");
  reader.parse_line("name=name2");
  reader.parse_line("port=port2");

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
  reader.parse_line("[bridge]");
  reader.parse_line("local.id=raw_a1");
  reader.parse_line("local.type=rawmidi");
  reader.parse_line("local.device=device");
  reader.parse_line("local.name=name");
  reader.parse_line("remote.id=raw_r1");
  reader.parse_line("remote.type=rtpmidi_connect");
  reader.parse_line("remote.name=name");
  reader.parse_line("remote.hostname=hostname");
  reader.parse_line("remote.port=remote_udp_port");
  reader.parse_line("remote.local_udp_port=local_udp_port");

  reader.parse_line("[bridge]");
  reader.parse_line("local.id=raw_a2");
  reader.parse_line("local.type=rawmidi");
  reader.parse_line("local.device=device2");
  reader.parse_line("local.name=name2");
  reader.parse_line("remote.id=raw_r2");
  reader.parse_line("remote.type=rtpmidi_connect");
  reader.parse_line("remote.name=name2");
  reader.parse_line("remote.hostname=hostname2");
  reader.parse_line("remote.port=remote_udp_port2");
  reader.parse_line("remote.local_udp_port=local_udp_port2");

  reader.parse_line("[web]");
  reader.parse_line("enabled=false");
  reader.parse_line("listen=0.0.0.0");
  reader.parse_line("port=9090");
  reader.parse_line("root=/var/www");
  reader.parse_line("username=u");
  reader.parse_line("password=p");
  ASSERT_EQUAL(settings.web.enabled, false);
  ASSERT_EQUAL(settings.web.listen, "0.0.0.0");
  ASSERT_EQUAL(settings.web.port, 9090);
  ASSERT_EQUAL(settings.web.root, "/var/www");
  ASSERT_EQUAL(settings.web.username, "u");
  ASSERT_EQUAL(settings.web.password, "p");

  reader.finish();
  rtpmididns::finalize_unified_ini_graph(settings, "test.ini");

  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 2u);
  ASSERT_EQUAL(settings.rtpmidi_announces[0].name, "name");
  ASSERT_EQUAL(settings.rtpmidi_announces[0].port, "port");
  ASSERT_EQUAL(settings.rtpmidi_announces[1].name, "name2");
  ASSERT_EQUAL(settings.rtpmidi_announces[1].port, "port2");

  ASSERT_EQUAL(settings.rawmidi.size(), 2u);
  ASSERT_EQUAL(settings.rawmidi[0].device, "device");
  ASSERT_EQUAL(settings.rawmidi[0].name, "name");
  ASSERT_EQUAL(settings.rawmidi[0].local_udp_port, "local_udp_port");
  ASSERT_EQUAL(settings.rawmidi[0].remote_udp_port, "remote_udp_port");
  ASSERT_EQUAL(settings.rawmidi[0].hostname, "hostname");
  ASSERT_EQUAL(settings.rawmidi[1].device, "device2");
  ASSERT_EQUAL(settings.rawmidi[1].name, "name2");
  ASSERT_EQUAL(settings.rawmidi[1].local_udp_port, "local_udp_port2");
  ASSERT_EQUAL(settings.rawmidi[1].remote_udp_port, "remote_udp_port2");
  ASSERT_EQUAL(settings.rawmidi[1].hostname, "hostname2");
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
  ASSERT_EQUAL(settings.control_filename, "test.ini");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, false);
  ASSERT_EQUAL(settings.rtpmidi_announces.size(), 1);
  ASSERT_EQUAL(settings.rtpmidi_announces[0].port, "1234");

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

  rtpmididns::settings_t w;
  rtpmididns::parse_argv(
      {
          "--web-disable",
          "--web-port",
          "7777",
      },
      &w);
  ASSERT_EQUAL(w.web.enabled, false);
  ASSERT_EQUAL(w.web.port, 7777);
  ASSERT_EQUAL(w.web.root, "frontend/dist");
}

void test_unified_ini_peer_factory(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  r.parse_line("id=ann1");
  r.parse_line("type=listen_rtpmidi");
  r.parse_line("name=MyHost");
  r.parse_line("port=5004");
  r.finish();
  rtpmididns::finalize_unified_ini_graph(s, "test.ini");
  ASSERT_EQUAL(s.ini_peers.size(), 0u);
  ASSERT_EQUAL(s.ini_connects.size(), 0u);
  ASSERT_EQUAL(s.rtpmidi_announces.size(), 1u);
  ASSERT_EQUAL(s.rtpmidi_announces[0].name, "MyHost");
  ASSERT_EQUAL(s.rtpmidi_announces[0].port, "5004");
}

void test_unified_ini_rawmidi_bridge(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  r.parse_line("id=raw1");
  r.parse_line("type=rawmidi");
  r.parse_line("device=/dev/snd/midiC0D0");
  r.parse_line("name=HW");
  r.parse_line("[peer]");
  r.parse_line("id=rtp1");
  r.parse_line("type=rtpmidi_listen");
  r.parse_line("name=HW");
  r.parse_line("local_udp_port=5104");
  r.parse_line("[connect]");
  r.parse_line("from=raw1");
  r.parse_line("to=rtp1");
  r.parse_line("[connect]");
  r.parse_line("from=rtp1");
  r.parse_line("to=raw1");
  r.finish();
  ASSERT_EQUAL(s.ini_peers.size(), 2u);
  ASSERT_EQUAL(s.ini_connects.size(), 2u);
  rtpmididns::finalize_unified_ini_graph(s, "test.ini");
  ASSERT_EQUAL(s.rawmidi.size(), 1u);
  ASSERT_EQUAL(s.rawmidi[0].device, "/dev/snd/midiC0D0");
  ASSERT_EQUAL(s.rawmidi[0].name, "HW");
  ASSERT_EQUAL(s.rawmidi[0].local_udp_port, "5104");
  ASSERT_EQUAL(s.rawmidi[0].hostname, "");
}

void test_unified_ini_bridge_section(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[bridge]");
  r.parse_line("local.id=r1");
  r.parse_line("local.type=rawmidi");
  r.parse_line("local.device=/dev/ttyUSB0");
  r.parse_line("local.name=Ser");
  r.parse_line("remote.id=r2");
  r.parse_line("remote.type=rtpmidi_connect");
  r.parse_line("remote.name=Ser");
  r.parse_line("remote.hostname=pi.local");
  r.parse_line("remote.port=5004");
  r.finish();
  rtpmididns::finalize_unified_ini_graph(s, "test.ini");
  ASSERT_EQUAL(s.rawmidi.size(), 1u);
  ASSERT_EQUAL(s.rawmidi[0].hostname, "pi.local");
  ASSERT_EQUAL(s.rawmidi[0].remote_udp_port, "5004");
}

void test_unified_ini_bridge_alsa_to_rtpmidi(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[bridge]");
  r.parse_line("local.id=a1");
  r.parse_line("local.type=alsa_listener");
  r.parse_line("local.name=DeepMind");
  r.parse_line("remote.id=c1");
  r.parse_line("remote.type=rtpmidi_connect");
  r.parse_line("remote.hostname=192.168.1.10");
  r.parse_line("remote.port=5004");
  r.parse_line("remote.local_udp_port=5010");
  r.finish();
  rtpmididns::finalize_unified_ini_graph(s, "test.ini");
  ASSERT_EQUAL(s.connect_to.size(), 1u);
  ASSERT_EQUAL(s.connect_to[0].name, "DeepMind");
  ASSERT_EQUAL(s.connect_to[0].hostname, "192.168.1.10");
  ASSERT_EQUAL(s.connect_to[0].port, "5004");
  ASSERT_EQUAL(s.connect_to[0].local_udp_port, "5010");
}

void test_unified_ini_duplicate_id_errors(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  r.parse_line("id=x");
  r.parse_line("type=listen_alsa_network");
  r.parse_line("name=N1");
  r.parse_line("[peer]");
  r.parse_line("id=x");
  r.parse_line("type=listen_alsa_network");
  r.parse_line("name=N2");
  r.finish();
  bool threw = false;
  try {
    rtpmididns::finalize_unified_ini_graph(s, "test.ini");
  } catch (const std::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_parse_ini), TEST(test_argv),
                       TEST(test_unified_ini_peer_factory),
                       TEST(test_unified_ini_rawmidi_bridge),
                       TEST(test_unified_ini_bridge_section),
                       TEST(test_unified_ini_bridge_alsa_to_rtpmidi),
                       TEST(test_unified_ini_duplicate_id_errors)};

  testcase.run(argc, argv);
  return testcase.exit_code();
}