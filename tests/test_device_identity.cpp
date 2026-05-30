/**
 * Phase 2: device_identity_t grammar tests.
 */
#include "../src/device_identity.hpp"
#include "test_case.hpp"
#include <string>

using namespace rtpmididns;

namespace {

device_identity_t make_id(std::string type,
                          std::vector<device_identity_field_t> fields) {
  return device_identity_t{std::move(type), std::move(fields)};
}

void assert_roundtrip(const std::string &canonical) {
  const auto parsed = device_identity_t::parse(canonical);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), canonical);
}

void assert_parse_fails(std::string_view text) {
  ASSERT_FALSE(device_identity_t::parse(text).has_value());
}

} // namespace

void test_device_identity_roundtrip_simple() {
  assert_roundtrip("alsa_seq:client=Peak,port=In");
  assert_roundtrip("rawmidi:device=/dev/snd/midiC4D0,name=MIDI Export");
  assert_roundtrip(
      "rtpmidi_client:hostname=host.local,port=5004,service=Peak Out");
  assert_roundtrip("rtpmidi_server:name=Peak InOut,port=5004");
}

void test_device_identity_roundtrip_bracketed() {
  assert_roundtrip("rtpmidi_server:name=Peak,[port=5004]");
  assert_roundtrip("alsa_seq:client=Peak,[port=In]");
  assert_roundtrip(
      "rtpmidi_client:hostname=host.local,[port=5004],service=Peak Out");
}

void test_device_identity_field_order_canonicalization() {
  const auto parsed = device_identity_t::parse(
      "rtpmidi_server:port=5004,name=Peak");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), "rtpmidi_server:name=Peak,port=5004");

  const auto built =
      make_id("alsa_seq", {{"port", "In", false}, {"client", "Peak", false}});
  ASSERT_EQUAL(built.serialize(), "alsa_seq:client=Peak,port=In");
}

void test_device_identity_bracketed_order_canonicalization() {
  const auto parsed = device_identity_t::parse(
      "rtpmidi_server:[port=5004],name=Peak");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->serialize(), "rtpmidi_server:name=Peak,[port=5004]");
}

void test_device_identity_escape_special_chars() {
  const auto parsed = device_identity_t::parse(
      R"(rawmidi:device=/dev/snd/midiC4D0,name=MIDI\, Export\: v1)");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQUAL(parsed->fields[1].value, "MIDI, Export: v1");
  ASSERT_EQUAL(parsed->serialize(),
               R"(rawmidi:device=/dev/snd/midiC4D0,name=MIDI\, Export\: v1)");

  const auto brackets = device_identity_t::parse(
      R"(alsa_seq:client=Peak,[port=\[In\]])");
  ASSERT_TRUE(brackets.has_value());
  ASSERT_TRUE(brackets->fields[1].bracketed);
  ASSERT_EQUAL(brackets->fields[1].value, "[In]");
  ASSERT_EQUAL(brackets->serialize(), R"(alsa_seq:client=Peak,[port=\[In\]])");
}

void test_device_identity_escape_roundtrip_unit() {
  const std::string raw = R"(a,b=c:d=e[f]g\)";
  const auto escaped = device_identity_t::escape(raw);
  ASSERT_EQUAL(device_identity_t::unescape(escaped), raw);
}

void test_device_identity_bracketed_preserved() {
  const auto id = make_id("rtpmidi_server",
                          {{"name", "Peak", false},
                           {"port", "5004", true}});
  ASSERT_EQUAL(id.serialize(), "rtpmidi_server:name=Peak,[port=5004]");

  const auto reparsed = device_identity_t::parse(id.serialize());
  ASSERT_TRUE(reparsed.has_value());
  ASSERT_TRUE(*reparsed == id);
}

void test_device_identity_equality() {
  const auto a = device_identity_t::parse("alsa_seq:client=Peak,port=In");
  const auto b = device_identity_t::parse("alsa_seq:port=In,client=Peak");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  ASSERT_TRUE(*a == *b);
}

void test_device_identity_parse_errors() {
  assert_parse_fails("");
  assert_parse_fails("no_colon");
  assert_parse_fails(":key=value");
  assert_parse_fails("type:");
  assert_parse_fails("type:key");
  assert_parse_fails("type:key=");
  assert_parse_fails("type:[key=value");
  assert_parse_fails("type:key=value,");
  assert_parse_fails("type:duplicate=1,duplicate=2");
}

int main(int argc, char **argv) {
  test_case_t testcase{
      TEST(test_device_identity_roundtrip_simple),
      TEST(test_device_identity_roundtrip_bracketed),
      TEST(test_device_identity_field_order_canonicalization),
      TEST(test_device_identity_bracketed_order_canonicalization),
      TEST(test_device_identity_escape_special_chars),
      TEST(test_device_identity_escape_roundtrip_unit),
      TEST(test_device_identity_bracketed_preserved),
      TEST(test_device_identity_equality),
      TEST(test_device_identity_parse_errors),
  };

  testcase.run(argc, argv);
  return testcase.exit_code();
}
