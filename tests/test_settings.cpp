/**
 * INI and argv parsing (identity-only [peer] / [connect] format).
 */
#include "ini.hpp"
#include "settings.hpp"
#include "test_case.hpp"
#include <argv.hpp>

void test_parse_ini(void) {
  rtpmididns::settings_t settings;
  rtpmididns::IniReader reader(&settings);

  reader.parse_line("[general]");
  reader.parse_line("alsa_name=testrtpmidid");
  reader.parse_line("control=/tmp/control.sock");

  ASSERT_EQUAL(settings.alsa_name, "testrtpmidid");
  ASSERT_EQUAL(settings.control_filename, "/tmp/control.sock");

  ASSERT_EQUAL(settings.ini_connects.size(), 0u);
  reader.parse_line("[connect]");
  reader.parse_line("from=rawmidi:device=/dev/a,name=A");
  reader.parse_line("to=rtpmidi_server:name=A,port=5104");
  reader.parse_line("direction=a2b");
  reader.parse_line("[connect]");
  reader.parse_line("from=rtpmidi_server:name=A,port=5104");
  reader.parse_line("to=rawmidi:device=/dev/a,name=A");

  ASSERT_EQUAL(settings.ini_connects.size(), 1u);
  ASSERT_EQUAL(settings.ini_connects[0].from, "rawmidi:device=/dev/a,name=A");
  ASSERT_EQUAL(settings.ini_connects[0].to, "rtpmidi_server:name=A,port=5104");
  ASSERT_TRUE(settings.ini_connects[0].direction.has_value());
  ASSERT_EQUAL(*settings.ini_connects[0].direction, "a2b");

  reader.finish();
  ASSERT_EQUAL(settings.ini_connects.size(), 2u);
  ASSERT_EQUAL(settings.ini_connects[1].from, "rtpmidi_server:name=A,port=5104");
  ASSERT_EQUAL(settings.ini_connects[1].to, "rawmidi:device=/dev/a,name=A");

  reader.parse_line("[peer]");
  reader.parse_line("identity=rtpmidi_multi:name=Host1,port=5004");
  reader.parse_line("[peer]");
  reader.parse_line("identity=alsa_multi:name=Network Export");

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
  ASSERT_EQUAL(settings.ini_peers.size(), 2u);
  ASSERT_EQUAL(settings.ini_peers[0].identity,
               "rtpmidi_multi:name=Host1,port=5004");
  ASSERT_EQUAL(settings.ini_peers[1].identity, "alsa_multi:name=Network Export");
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
  ASSERT_EQUAL(settings.control_filename, "test.ini");
  ASSERT_EQUAL(settings.rtpmidi_discover.enabled, false);
  ASSERT_EQUAL(settings.ini_peers.size(), 1u);
  ASSERT_TRUE(settings.ini_peers[0].identity.find("port=1234") !=
              std::string::npos);

  rtpmididns::parse_argv({{"--version"}}, &settings);

  settings.ini_peers.clear();
  rtpmididns::parse_argv({{"--rawmidi="}}, &settings);
  ASSERT_EQUAL(settings.ini_peers.size(), 0u);

  rtpmididns::settings_t w;
  rtpmididns::parse_argv({{"--web-disable"}, {"--web-port", "7777"}}, &w);
  ASSERT_EQUAL(w.web.enabled, false);
  ASSERT_EQUAL(w.web.port, 7777);
  ASSERT_EQUAL(w.web.root, "frontend/dist");
}

void test_ini_identity_peer_and_connect(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  r.parse_line("identity=rawmidi:device=/dev/snd/midiC0D0,name=HW");
  r.parse_line("[peer]");
  r.parse_line("identity=rtpmidi_server:name=HW,port=5104");
  r.parse_line("[connect]");
  r.parse_line("from=rawmidi:device=/dev/snd/midiC0D0,name=HW");
  r.parse_line("to=rtpmidi_server:name=HW,port=5104");
  r.parse_line("[connect]");
  r.parse_line("from=rtpmidi_server:name=HW,port=5104");
  r.parse_line("to=rawmidi:device=/dev/snd/midiC0D0,name=HW");
  r.finish();
  ASSERT_EQUAL(s.ini_peers.size(), 2u);
  ASSERT_EQUAL(s.ini_connects.size(), 2u);
  ASSERT_EQUAL(s.ini_peers[0].identity,
               "rawmidi:device=/dev/snd/midiC0D0,name=HW");
}

void test_ini_rejects_bridge_section(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[bridge]");
  bool threw = false;
  try {
    r.parse_line("local.id=x");
  } catch (const std::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

void test_ini_rejects_legacy_peer_type(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  bool threw = false;
  try {
    r.parse_line("type=listen_rtpmidi");
  } catch (const std::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

void test_ini_invalid_identity(void) {
  rtpmididns::settings_t s;
  rtpmididns::IniReader r(&s);
  r.set_filename("test.ini");
  r.parse_line("[peer]");
  r.parse_line("identity=not-a-valid-identity");
  bool threw = false;
  try {
    r.finish();
  } catch (const std::exception &) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

int main(int argc, char **argv) {
  test_case_t testcase{TEST(test_parse_ini), TEST(test_argv),
                       TEST(test_ini_identity_peer_and_connect),
                       TEST(test_ini_rejects_bridge_section),
                       TEST(test_ini_rejects_legacy_peer_type),
                       TEST(test_ini_invalid_identity)};

  testcase.run(argc, argv);
  return testcase.exit_code();
}
